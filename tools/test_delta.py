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
    # (hosts announce a version of the table, which may be older than its
    # newest: clients play by a newer host's version before our hosts do)
    assert define(limits, "HALO_PORT_NETWORK_VERSION") in [number for number, _, _ in table], \
        "hosts must announce a version of the table"
    assert define(limits, "HALO_PORT_NETWORK_VERSION_MAXIMUM") >= newest, \
        "clients must join hosts up to the table's newest version, at least"
    assert define(DELTA.read_text(encoding="utf-8"), "DELTA_LEGACY_NEWEST") == newest, \
        "a Delta client's HELLO says the table's newest version, the newest whose rules it plays"
    assert define(limits, "HALO_PORT_NETWORK_VERSION_MINIMUM") == last_breaking, \
        "clients must join hosts back to the table's newest breaking version"
    presumed = define(DELTA.read_text(encoding="utf-8"), "DELTA_LEGACY_PRESUMED")
    assert presumed >= 1
    assert define(limits, "HALO_PORT_NETWORK_VERSION_MAXIMUM") >= newest + presumed, \
        "clients must join hosts of the versions past the table's newest presumed additive (DELTA_LEGACY_PRESUMED)"


def test_advertised_wire_fits():
    text = DELTA.read_text(encoding="utf-8")
    wire = re.search(r'^#define DELTA_WIRE "([^"]+)"', text, re.M).group(1)
    assert len(wire) <= define(text, "DELTA_ADVERTISED_WIRE_SIZE")
    # (the major and the wire after the port's version and flags, within the
    # advertisement's 0x1A reserved bytes)
    assert define(text, "DELTA_ADVERTISED_WIRE_OFFSET") >= 3
    assert define(text, "DELTA_ADVERTISED_WIRE_OFFSET") + 1 + define(text, "DELTA_ADVERTISED_WIRE_SIZE") <= 0x1A


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
    assert {name: delta_table.numbers(row) for name, row in table["wires"].items()} == \
        {delta_table.wire(): delta_table.built_in()}


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
    b'{"delta_legacy": 1, "serial": 4294967295, "wires": {}}',
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


@pytest.fixture(scope="module")
def recovery_keys(keys, tmp_path_factory):
    """a second test key pair, the tests' recovery key"""
    directory = tmp_path_factory.mktemp("delta_recovery_key")
    private = directory / "delta_test_recovery_key.pem"
    return private, delta_table.keygen(private)


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


def make_table(tmp_path, *arguments):
    return subprocess.run([sys.executable, str(ROOT / "tools" / "delta_table.py"), "make", *arguments],
                          capture_output=True, text=True)


def test_make_row_widens_only(tmp_path):
    old = tmp_path / "old.json"
    old.write_bytes(document(serial=4, row=floor()))
    own = delta_table.wire()
    wide = f"{floor()['announce'] + 2},{floor()['minimum']},{floor()['maximum'] + 2}"
    result = make_table(tmp_path, "--serial", "5", "--from", str(old), "--row", f"{own}={wide}",
                        "--row", "chupa-99z=30,25,31")
    assert result.returncode == 0, result.stderr
    table = json.loads(result.stdout)
    assert table["wires"][own]["announce"] == floor()["announce"] + 2
    assert table["wires"]["chupa-99z"] == {"announce": 30, "minimum": 25, "maximum": 31}
    narrow = f"{floor()['announce'] - 1},{floor()['minimum']},{floor()['maximum']}"
    assert make_table(tmp_path, "--serial", "5", "--from", str(old), "--row", f"{own}={narrow}").returncode == 1
    result = make_table(tmp_path, "--serial", "5", "--from", str(old), "--row", f"{own}={wide}@build-145")
    assert json.loads(result.stdout)["wires"][own]["follows"] == "build-145"
    for bad in ("chupa-20a=22,23,22", "chupa-20a=22", "Chupa=1,1,1", "chupa-20a=0,0,0", "chupa-20a=22,11,22@build 1",
                "chupa-20a=22,11,22@" + "b" * 32):
        assert make_table(tmp_path, "--serial", "5", "--row", bad).returncode == 1, bad


def test_make_take_back(tmp_path):
    old = tmp_path / "old.json"
    wide = dict(floor(), announce=floor()["announce"] + 2, maximum=floor()["maximum"] + 2)
    old.write_bytes(document(serial=4, row=wide, extra={"chupa-01z": {"announce": 30, "minimum": 11, "maximum": 30}}))
    own = delta_table.wire()
    shipped = f"{floor()['announce']},{floor()['minimum']},{floor()['maximum']}"
    result = make_table(tmp_path, "--serial", "5", "--from", str(old), "--take-back", f"{own}={shipped}",
                        "--take-back", "chupa-01z=20,11,20")
    assert result.returncode == 0, result.stderr
    table = json.loads(result.stdout)
    assert delta_table.numbers(table["wires"][own]) == floor()
    assert table["wires"]["chupa-01z"] == {"announce": 20, "minimum": 11, "maximum": 20}
    below = f"{floor()['announce'] - 1},{floor()['minimum']},{floor()['maximum']}"
    assert make_table(tmp_path, "--serial", "5", "--from", str(old), "--take-back", f"{own}={below}").returncode == 1


def test_epochs():
    assert delta_table.epoch(3) == 0 and delta_table.epoch(0x01000001) == 1
    assert delta_table.next_serial(3) == 4
    assert delta_table.next_serial(3, new_epoch=True) == 0x01000001
    assert delta_table.next_serial(0x01000001, new_epoch=True) == 0x02000001
    with pytest.raises(delta_table.TableError):
        delta_table.next_serial(0x00FFFFFF)
    assert delta_table.header_last_epochs() == [0, 255]


def test_check_epochs(keys, recovery_keys):
    data = document(serial=0x01000001)
    with pytest.raises(delta_table.TableError):
        delta_table.check(data, signed(keys, data).hex(), [keys[1], recovery_keys[1]], [0, 255])
    assert delta_table.check(data, signed(recovery_keys, data).hex(), [keys[1], recovery_keys[1]],
                             [0, 255])["serial"] == 0x01000001


def test_make_from_keeps_other_wires(tmp_path):
    old = tmp_path / "old.json"
    old.write_bytes(document(serial=4, extra={"chupa-17a": {"announce": 17, "minimum": 11, "maximum": 19}}))
    result = subprocess.run([sys.executable, str(ROOT / "tools" / "delta_table.py"), "make", "--serial", "5",
                             "--from", str(old)], capture_output=True, text=True, check=True)
    table = json.loads(result.stdout)
    assert table["wires"]["chupa-17a"]["maximum"] == 19
    # (a published row wider than the built-in numbers stays: making the table
    # again from the same commit never narrows what builds follow)
    assert delta_table.numbers(table["wires"][delta_table.wire()]) == dict(floor(), announce=floor()["announce"] + 1,
                                                                           maximum=floor()["maximum"] + 1)
    result = subprocess.run([sys.executable, str(ROOT / "tools" / "delta_table.py"), "make", "--serial", "4",
                             "--from", str(old)], capture_output=True, text=True)
    assert result.returncode == 1


# ---------- the game's loader (delta.c), through tools/delta_check.c


@pytest.fixture(scope="module")
def checker(keys, recovery_keys, tmp_path_factory):
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
    recovery = ",".join(f"0x{byte:02x}" for byte in recovery_keys[1])
    program = tmp_path_factory.mktemp("delta_check") / "delta_check"
    link = ["-Wl,-undefined,dynamic_lookup", "-Wl,-dead_strip"] if sys.platform == "darwin" else \
        ["-no-pie", "-Wl,--unresolved-symbols=ignore-all"]
    # (a failed fetch is tried again a tenth of a second later, not thirty minutes)
    built = subprocess.run(["clang", *flags, "-DHALO_GAME_BROWSER", f"-DHALO_DELTA_TEST_KEY={key}",
                            f"-DHALO_DELTA_TEST_RECOVERY_KEY={recovery}",
                            "-DDELTA_RETRY_INTERVAL=100", "-O1", *link, "-o", str(program),
                            *sources, "tools/delta_check.c", "port/linux/game/network_rules.c",
                            "port/third_party/monocypher/monocypher.c",
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


def test_loader_presumes_the_next_version(keys, checker, tmp_path):
    # (a row that announces a newer number without widening the join: the
    # next ones, DELTA_LEGACY_PRESUMED, are joined anyway)
    base = floor()
    presumed = define(DELTA.read_text(encoding="utf-8"), "DELTA_LEGACY_PRESUMED")
    announce = base["maximum"] + 3
    table = write_signed(keys, tmp_path / "t10", document(serial=10, row=dict(base, announce=announce, maximum=announce)))
    output = run_checker(checker, tmp_path, "offer", table, "state")
    assert "offer 1" in output
    assert state_line(output)[0].startswith(f"state {announce} {base['minimum']} {announce + presumed} serial 10 ")


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
    ("serial of none", lambda: document(serial=0xFFFFFFFF)),
    ("nul", lambda: document().replace(b'"issued"', b'"\x00": 1, "issued"')),
])
def test_loader_drops_bad_tables(keys, checker, tmp_path, name, make):
    table = write_signed(keys, tmp_path / "bad", make())
    output = run_checker(checker, tmp_path, "offer", table, "state")
    assert "offer 0" in output, name
    assert "log: Delta: dropped" in output, name
    assert " serial 0 " in state_line(output)[0], name
    assert not (tmp_path / "delta_legacy.signed").exists()


def test_follows(keys, checker, tmp_path):
    row = dict(floor(), announce=floor()["announce"] + 1, maximum=floor()["maximum"] + 1)
    assert run_checker(checker, tmp_path, "following").strip().endswith(
        f"following Following OpenCE network version {floor()['announce']} (built in)")
    table = write_signed(keys, tmp_path / "t11", document(serial=11, row=dict(row, follows="build-145")))
    output = run_checker(checker, tmp_path, "offer", table, "following")
    assert "offer 1" in output and "following OpenCE build-145" in output
    assert "following Following OpenCE build-145 (table 11)" in output
    # (a newer table without it: the number)
    table = write_signed(keys, tmp_path / "t12", document(serial=12, row=row))
    output = run_checker(checker, tmp_path, "offer", table, "following")
    assert f"following Following OpenCE network version {row['announce']} (table 12)" in output


@pytest.mark.parametrize("follows", ['"build 145"', '"' + "b" * 32 + '"', "145", '""'])
def test_follows_checked(keys, checker, tmp_path, follows):
    data = document(serial=11).replace(b'"maximum": ', b'"follows": ' + follows.encode() + b', "maximum": ', 1)
    with pytest.raises(delta_table.TableError):
        delta_table.parse(data)
    output = run_checker(checker, tmp_path, "offer", write_signed(keys, tmp_path / "bad", data), "state")
    assert "offer 0" in output


def test_loader_epochs(keys, recovery_keys, checker, tmp_path):
    """a leaked primary key at its highest serial is followed by the recovery
    key's next epoch, which the primary can never sign"""
    highest = write_signed(keys, tmp_path / "highest", document(serial=0x00FFFFFF))
    primary_next = write_signed(keys, tmp_path / "primary_next", document(serial=0x01000001))
    recovery_next = write_signed(recovery_keys, tmp_path / "recovery_next", document(serial=0x01000001),
                                 signed(recovery_keys, document(serial=0x01000001)))
    primary_after = write_signed(keys, tmp_path / "primary_after", document(serial=0x01000002))
    recovery_after = write_signed(recovery_keys, tmp_path / "recovery_after", document(serial=0x01000002),
                                  signed(recovery_keys, document(serial=0x01000002)))
    output = run_checker(checker, tmp_path, "offer", highest, "offer", primary_next, "state", "offer", recovery_next,
                         "state", "offer", primary_after, "offer", recovery_after, "state")
    offers = [line for line in output.splitlines() if line.startswith("offer ")]
    assert offers == ["offer 1", "offer 0", "offer 1", "offer 0", "offer 1"]
    states = state_line(output)
    assert f" serial {0x00FFFFFF} " in states[0]
    assert f" serial {0x01000001} " in states[1]
    assert f" serial {0x01000002} " in states[2]
    assert "its epoch (1) is past its key's last (0)" in output
    # (a recovery-signed table of epoch 0 is taken as any other)
    table = write_signed(recovery_keys, tmp_path / "recovery_old", document(serial=12),
                         signed(recovery_keys, document(serial=12)))
    other = tmp_path / "other"
    other.mkdir()
    assert "offer 1" in run_checker(checker, other, "offer", table)


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


# ---------- wires and layouts


# each released wire's newest version (DELTA_WIRE_LEGACY_NEWEST): never
# changed once released, a build that learns a newer version's rules has a
# new wire (docs/delta.md, "When the wire changes")
RELEASED_WIRE_NEWEST = {"chupa-28a": 28, "chupa-29a": 29, "chupa-32a": 33, "chupa-38a": 38}


def test_wire_changes_with_the_rules():
    """a build that learns a version's rules (a newer DELTA_LEGACY_NEWEST, a
    row of DELTA_LEGACY_VERSIONS) has a new wire: a signed table's row widens
    every build of a wire alike"""
    text = DELTA.read_text(encoding="utf-8")
    ledger = delta_table.wire_newest()
    for name, number in RELEASED_WIRE_NEWEST.items():
        assert ledger.get(name) == number, f"{name}'s row of DELTA_WIRE_LEGACY_NEWEST changed (or went)"
    assert delta_table.wire() in ledger, "DELTA_WIRE has no row in DELTA_WIRE_LEGACY_NEWEST"
    assert ledger[delta_table.wire()] == define(text, "DELTA_LEGACY_NEWEST") == rows()[-1][0], \
        "a build that learns a newer version's rules (DELTA_LEGACY_VERSIONS' newest) needs a new DELTA_WIRE"
    # (each wire's row once, in order, and each plays with older wires)
    names = list(ledger)
    numbers = [int(re.fullmatch(r"chupa-(\d+)[a-z]", name).group(1)) for name in names]
    assert numbers == sorted(set(numbers))
    assert list(ledger.values()) == sorted(ledger.values())
    compatibility = text[text.index("#define DELTA_WIRE_COMPATIBILITY(X)"):]
    compatibility = compatibility[:compatibility.index("\n\n")]
    assert f'X("{delta_table.wire()}", ' in compatibility
    assert delta_table.reads_layouts(delta_table.wire())


C_LAYOUT = r'''
#include "network_rules.h"
#include "../include/delta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* facts from the arguments: VERSION SETTINGS OPTIONS SHIELD KIND+KIND ... (0 not said) */
int main(int count, char **arguments)
{
	static struct delta_layout_fact facts[DELTA_LAYOUT_FACTS];
	int used = 0, index;
	unsigned version, kind;

	for (index = 1; index + 4 < count && used < DELTA_LAYOUT_FACTS; index += 5, used++)
	{
		char *kinds = arguments[index + 4];

		facts[used].version = (unsigned short)atoi(arguments[index]);
		facts[used].settings_size = (unsigned short)atoi(arguments[index + 1]);
		facts[used].options_size = (unsigned short)atoi(arguments[index + 2]);
		facts[used].shield_effects_maximum = (unsigned short)atoi(arguments[index + 3]);
		while (*kinds)
		{
			unsigned value = (unsigned)strtoul(kinds, &kinds, 10);

			if (value && value < 256)
				facts[used].dropped[value >> 3] |= (unsigned char)(1 << (value & 7));
			if (*kinds == '+')
				kinds++;
			else if (*kinds)
				return 2;
		}
	}
	for (version = 11; version <= 64; version++)
	{
		struct network_rules_layout layout;

		network_rules_layout_built_in(version, &layout);
		network_rules_layout_apply(&layout, facts, used);
		printf("%u %d %d %d", version, layout.settings_size, layout.options_size, layout.shield_effects_maximum);
		for (kind = 1; kind < 256; kind++)
			if (network_rules_layout_drops(&layout, kind))
				printf(" %u", kind);
		printf(" refused %d\n", layout.refused);
	}
	return 0;
}
'''


@pytest.fixture(scope="module")
def layout_program(tmp_path_factory):
    cc = shutil.which("cc") or shutil.which("clang")
    if not cc:
        pytest.skip("no C compiler")
    directory = tmp_path_factory.mktemp("layouts")
    game = ROOT / "port" / "linux" / "game"
    source = game / "network_rules_layout_check.c"
    try:
        source.write_text(C_LAYOUT, encoding="utf-8")
        subprocess.run([cc, "-std=c11", "-Wall", "-Werror", str(source), str(game / "network_rules.c"), "-o",
                        str(directory / "layouts")], check=True, capture_output=True, text=True)
    finally:
        source.unlink(missing_ok=True)
    return directory / "layouts"


def c_layouts(program, facts):
    arguments = []
    for version, fact in facts.items():
        arguments += [version, str(fact.get("settings_size", 0)), str(fact.get("options_size", 0)),
                      str(fact.get("shield_effects_maximum", 0)),
                      "+".join(str(kind) for kind in fact.get("dropped_messages", []))]
    output = subprocess.run([str(program), *arguments], capture_output=True, text=True, check=True).stdout
    layouts = {}
    for line in output.splitlines():
        words = line.split()
        layouts[int(words[0])] = ({"settings_size": int(words[1]), "options_size": int(words[2]),
                                   "shield_effects_maximum": int(words[3]),
                                   "dropped_messages": [int(word) for word in words[4:-2]]}, int(words[-1]))
    return layouts


def test_tool_layouts_are_the_games(layout_program):
    """delta_table.py reads layouts as network_rules.c does: the built-in
    facts, and a table's facts applied within the limits (so the tool refuses
    what a build would not take)"""
    import random

    own = define(DELTA.read_text(encoding="utf-8"), "DELTA_LEGACY_NEWEST")
    for version, (layout, refused) in c_layouts(layout_program, {}).items():
        assert layout == delta_table.built_in_layout(version) and refused == 0, version
    generator = random.Random(4)
    for _ in range(40):
        facts = {}
        for version in sorted(generator.sample(range(30, 60), generator.randint(1, 6))):
            fact = {}
            if generator.random() < 0.6:
                fact["options_size"] = generator.choice([0x1C, 0x2C, 0x30, 0x40, 0x6C, 0x6D, 0x2A])
            if generator.random() < 0.5:
                fact["settings_size"] = generator.choice([13120, 13136, 13140, 13150, 13200, 13201, 13190])
            if generator.random() < 0.4:
                fact["shield_effects_maximum"] = generator.randint(1, 17)
            if generator.random() < 0.4:
                fact["dropped_messages"] = sorted(generator.sample(range(1, 256), 3))
            facts[str(version)] = fact
        games = c_layouts(layout_program, facts)
        for version, (layout, refused) in games.items():
            tool, tool_refused = delta_table.apply_layouts(version, facts, own)
            assert tool == layout, (facts, version)
            assert len(tool_refused) == refused, (facts, version)


def test_make_has_this_builds_facts():
    table = json.loads(delta_table.make(7, issued=1))
    row = table["wires"][delta_table.wire()]
    own = define(DELTA.read_text(encoding="utf-8"), "DELTA_LEGACY_NEWEST")
    assert [int(version) for version in row["layouts"]] == list(range(floor()["announce"] + 1, own + 1))
    assert row["layouts"]["35"] == {"settings_size": 13136, "options_size": 44}
    assert row["layouts"]["36"]["dropped_messages"] == [91]
    delta_table.parse(delta_table.make(7, issued=1), delta_table.wire())


def test_make_layouts(tmp_path):
    own = delta_table.wire()
    old = tmp_path / "old.json"
    old.write_bytes(delta_table.make(4, issued=1))
    result = make_table(tmp_path, "--serial", "5", "--from", str(old),
                        "--layout", f"{own}=39:options_size=48,dropped_messages=92+93",
                        "--layout", f"{own}=40:shield_effects_maximum=7")
    assert result.returncode == 0, result.stderr
    (tmp_path / "new.json").write_text(result.stdout)
    row = json.loads(result.stdout)["wires"][own]
    assert row["layouts"]["39"] == {"options_size": 48, "dropped_messages": [92, 93]}
    assert row["layouts"]["40"] == {"shield_effects_maximum": 7}
    # (made again, and with a --row for the wire: the facts past its own stay)
    wide = f"{floor()['announce'] + 6},{floor()['minimum']},{floor()['maximum'] + 2}"
    result = make_table(tmp_path, "--serial", "6", "--from", str(tmp_path / "new.json"), "--row", f"{own}={wide}")
    assert result.returncode == 0, result.stderr
    row = json.loads(result.stdout)["wires"][own]
    assert row["announce"] == floor()["announce"] + 6 and row["layouts"]["39"]["options_size"] == 48
    assert "33" in row["layouts"]
    for bad in (f"{own}=38:options_size=48", f"{own}=39:options_size=40", f"{own}=39:settings_size=13300",
                f"{own}=39:shield_effects_maximum=16", f"{own}=39:shield_effects_maximum=5",
                f"{own}=39:dropped_messages=0", f"{own}=39:dropped_messages=256", f"{own}=39:colour=1",
                f"{own}=39:options_size=48,options_size=52", f"{own}=39", "chupa-32a=39:options_size=48",
                "chupa-99z=100:options_size=48"):
        assert make_table(tmp_path, "--serial", "6", "--from", str(old), "--layout", bad).returncode == 1, bad


@pytest.mark.parametrize("layouts", [
    [], {"39": []}, {"039": {}}, {"0": {}}, {"70000": {}}, {"x": {}},
    {"39": {"options_size": -1}}, {"39": {"options_size": 1.5}}, {"39": {"settings_size": 70000}},
    {"39": {"dropped_messages": [0]}}, {"39": {"dropped_messages": [256]}}, {"39": {"dropped_messages": 92}},
    {str(version): {} for version in range(39, 39 + 17)},
    {"39": {"x": {"y": {"z": [[1]]}}}},
])
def test_bad_layouts(layouts):
    with pytest.raises(delta_table.TableError):
        delta_table.parse(document(extra={"chupa-40a": {"announce": 32, "minimum": 11, "maximum": 39,
                                                         "layouts": layouts}}))


def layout_line(output, version):
    return next(line for line in output.splitlines() if line.startswith(f"layout {version} "))


def test_loader_reads_layouts(keys, checker, tmp_path):
    """this wire's row's layouts: read, and used past the build's own
    versions within the limits; the built-in facts for its own versions"""
    row = dict(floor(), layouts={"35": {"options_size": 60}, "39": {"options_size": 48, "dropped_messages": [92, 93],
                                                                    "shield_effects_maximum": 7, "later": {"a": [1]}},
                                  "41": {"settings_size": 13146}, "42": {"options_size": 20}})
    table = write_signed(keys, tmp_path / "t10", document(serial=10, row=row))
    output = run_checker(checker, tmp_path, "layout", 39, "offer", table, "layout", 35, "layout", 38, "layout", 39,
                         "layout", 41, "layout", 42, "state")
    assert "offer 1" in output and "4 layout facts" in output
    assert layout_line(output, 39).startswith("layout 39 settings 13136 options 44 shield 6 dropped 91 ")
    lines = [line for line in output.splitlines() if line.startswith("layout ")][1:]
    assert lines[0] == "layout 35 settings 13136 options 44 shield 6 dropped - learned 0 refused 0 facts 4"
    assert lines[1].startswith("layout 38 settings 13136 options 44 shield 6 dropped 91 learned 0 refused 0")
    assert lines[2].startswith("layout 39 settings 13140 options 48 shield 7 dropped 91,92,93 ")
    assert lines[3].startswith("layout 41 settings 13146 options 48 shield 7 ")
    assert lines[4].startswith("layout 42 settings 13146 options 48 shield 7 ") and " refused 1 " in lines[4]
    # (the cached table, read again at the next start)
    output = run_checker(checker, tmp_path, "layout", 39)
    assert layout_line(output, 39).startswith("layout 39 settings 13140 options 48 ")


def test_loader_skips_other_wires_layouts(keys, checker, tmp_path):
    """a newer wire's row with layouts, as this build's parser passes over
    another wire's row (as every older build's does, the same way)"""
    other = {"announce": 40, "minimum": 11, "maximum": 41,
             "layouts": {"39": {"options_size": 48, "dropped_messages": [92], "later": {"a": [1, 2]}}}}
    table = write_signed(keys, tmp_path / "t10", document(serial=10, extra={"chupa-99z": other}))
    output = run_checker(checker, tmp_path, "offer", table, "layout", 39, "state")
    assert "offer 1" in output
    assert layout_line(output, 39).startswith("layout 39 settings 13136 options 44 ")
    assert f" {floor()['maximum'] + 1} serial 10 " in state_line(output)[0]


@pytest.mark.parametrize("name,layouts", [
    ("twice", b'{"39": {}, "39": {}}'),
    ("leading zero", b'{"039": {}}'),
    ("zero", b'{"0": {}}'),
    ("escaped", b'{"\\u0033\\u0039": {}}'),
    ("too many", ("{" + ", ".join(f'"{version}": {{}}' for version in range(39, 56)) + "}").encode()),
    ("kind 0", b'{"39": {"dropped_messages": [0]}}'),
    ("kind 256", b'{"39": {"dropped_messages": [256]}}'),
    ("field twice", b'{"39": {"options_size": 48, "options_size": 52}}'),
    ("negative", b'{"39": {"options_size": -4}}'),
    ("not an object", b'[39]'),
    ("layouts twice", b'{}, "layouts": {}'),
])
def test_loader_drops_bad_layouts(keys, checker, tmp_path, name, layouts):
    data = document(serial=11).replace(b'"maximum": ', b'"layouts": ' + layouts + b', "maximum": ', 1)
    output = run_checker(checker, tmp_path, "offer", write_signed(keys, tmp_path / "bad", data), "state")
    assert "offer 0" in output, name
    assert " serial 0 " in state_line(output)[0], name


def test_loader_override_layouts(checker, tmp_path):
    """network.legacy_table: a server announces a number of its own, with
    layouts, unsigned and never passed on"""
    local = tmp_path / "local.json"
    local.write_bytes(document(serial=1, row={"announce": 38, "minimum": 11, "maximum": 39,
                                              "layouts": {"39": {"options_size": 48}}}))
    output = run_checker(checker, tmp_path, "state", "layout", 39, override=local)
    assert state_line(output)[0] == "state 38 11 39 serial 0 override 1 disabled 0 signed 0"
    assert "1 layout facts" in output
    assert layout_line(output, 39).startswith("layout 39 settings 13140 options 48 ")
