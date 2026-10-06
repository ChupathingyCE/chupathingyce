#!/usr/bin/env python3
"""Makes, signs and checks Delta's legacy table (docs/delta.md, "The legacy
table as config"; the game's side is port/linux/src/delta.c).

The table is a JSON document, legacy.json:

    {"delta_legacy": 1, "serial": 42, "issued": 1791331200,
     "wires": {"chupa-20a": {"announce": 21, "minimum": 11, "maximum": 21}},
     "disabled_capabilities": [],
     "platform_policy": {"xbox": {"host_players": 16}}}

("platform_policy" is optional and reserved: per-platform limits Delta Peer
reads, each bounded by the build's own hard limits), and its signature,
legacy.json.sig: the Ed25519 signature of legacy.json's exact bytes, as 128
hex digits and a line feed. A build reads only its own
wire's row (DELTA_WIRE in port/linux/include/delta.h), and takes it only if
it widens the numbers the build was made with. At most 16384 bytes.

    delta_table.py make --serial N [--from OLD.json] [--out legacy.json]
        this build's wire and numbers (delta.h, halo_port_limits.h) as a
        table; --from keeps another table's rows for the other wires
    delta_table.py sign --key KEY.pem legacy.json      writes legacy.json.sig
    delta_table.py verify [--public-key HEX] legacy.json
        checks legacy.json.sig against the key (or delta_key.h's keys)
    delta_table.py keygen --out KEY.pem
        a new key pair: the private key into KEY.pem (readable by you alone:
        keep it out of every repository; CI has it as a secret), the public
        half printed, as hex and as delta_key.h's C array

It uses the openssl command (OpenSSL 3, or the one $OPENSSL names) for
Ed25519, so nothing beyond Python's own library is needed.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DELTA = ROOT / "port" / "linux" / "include" / "delta.h"
LIMITS = ROOT / "port" / "linux" / "include" / "halo_port_limits.h"
KEY_HEADER = ROOT / "port" / "linux" / "src" / "delta_key.h"

FORMAT = 1
DOCUMENT_SIZE = 16384
MAXIMUM_VERSION = 65535
# platform_policy (reserved for Delta Peer's per-platform limits): platform
# and limit names, and each limit a small whole number
NAME = re.compile(r"[a-z0-9_]{1,32}")
POLICY_MAXIMUM = 4096
# (an Ed25519 public key's DER: this prefix, then its 32 bytes)
ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")


class TableError(ValueError):
    pass


# ---------- the build's own numbers


def wire() -> str:
    match = re.search(r'^#define DELTA_WIRE "([^"]+)"', DELTA.read_text(encoding="utf-8"), re.M)
    if not match:
        raise SystemExit(f"no DELTA_WIRE in {DELTA}")
    return match.group(1)


def built_in() -> dict:
    """the numbers this build was made with (halo_port_limits.h)"""
    text = LIMITS.read_text(encoding="utf-8")

    def define(name):
        match = re.search(rf"^#define {name} (\d+)", text, re.M)
        if not match:
            raise SystemExit(f"{name} is not a number in {LIMITS}")
        return int(match.group(1))

    return {"announce": define("HALO_PORT_NETWORK_VERSION"),
            "minimum": define("HALO_PORT_NETWORK_VERSION_MINIMUM"),
            "maximum": define("HALO_PORT_NETWORK_VERSION_MAXIMUM")}


# ---------- the document


def _no_duplicates(pairs):
    keys = [key for key, _ in pairs]
    if len(keys) != len(set(keys)):
        raise TableError("a key appears twice")
    return dict(pairs)


def _integer(value, minimum, maximum, name):
    if type(value) is not int or not minimum <= value <= maximum:
        raise TableError(f"{name} is not a whole number from {minimum} to {maximum}")
    return value


def parse(document: bytes, own_wire: str = None) -> dict:
    """the table, checked as the game checks it (delta.c), or TableError"""
    if len(document) > DOCUMENT_SIZE:
        raise TableError(f"it is {len(document)} bytes, more than {DOCUMENT_SIZE}")
    try:
        table = json.loads(document.decode("utf-8"), object_pairs_hook=_no_duplicates,
                           parse_constant=lambda name: (_ for _ in ()).throw(TableError(name)))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise TableError(f"it is not JSON: {error}") from None
    if not isinstance(table, dict):
        raise TableError("it is not an object")
    for key in ("delta_legacy", "serial", "wires"):
        if key not in table:
            raise TableError(f"it has no {key}")
    if _integer(table["delta_legacy"], 0, 1000000, "delta_legacy") != FORMAT:
        raise TableError(f"its format is {table['delta_legacy']}, not {FORMAT}")
    _integer(table["serial"], 1, 2 ** 32 - 1, "serial")
    if "issued" in table:
        _integer(table["issued"], 0, 2 ** 53, "issued")
    if not isinstance(table["wires"], dict):
        raise TableError("wires is not an object")
    for name, row in table["wires"].items():
        if not isinstance(row, dict) or not all(key in row for key in ("announce", "minimum", "maximum")):
            raise TableError(f"wire {name}'s row has no announce, minimum and maximum")
        for key in ("announce", "minimum", "maximum"):
            _integer(row[key], 1, MAXIMUM_VERSION, f"wire {name}'s {key}")
        if not row["minimum"] <= row["announce"] <= row["maximum"]:
            raise TableError(f"wire {name}'s row is not a range")
    policy = table.get("platform_policy", {})
    if not isinstance(policy, dict):
        raise TableError("platform_policy is not an object")
    for platform, limits in policy.items():
        if not NAME.fullmatch(platform) or not isinstance(limits, dict):
            raise TableError(f"platform_policy's {platform!r} is not a platform's limits")
        for name, value in limits.items():
            if not NAME.fullmatch(name):
                raise TableError(f"platform_policy's {platform} has a limit named {name!r}")
            _integer(value, 0, POLICY_MAXIMUM, f"platform_policy's {platform}.{name}")
    disabled = table.get("disabled_capabilities", [])
    if not isinstance(disabled, list) or len(disabled) > 64 or not all(isinstance(name, str) for name in disabled):
        raise TableError("disabled_capabilities is not a list of names")
    if own_wire is not None:
        check_widens(table, own_wire, built_in())
    return table


def check_widens(table: dict, own_wire: str, floor: dict) -> None:
    """a row may only widen the built-in numbers (as the game takes it)"""
    row = table["wires"].get(own_wire)
    if row is None:
        return
    if row["announce"] < floor["announce"] or row["minimum"] > floor["minimum"] or \
            row["maximum"] < floor["maximum"]:
        raise TableError(f"wire {own_wire}'s row {row} narrows the built-in {floor}")


def make(serial: int, issued: int = None, previous: dict = None) -> bytes:
    wires = dict(previous["wires"]) if previous else {}
    wires[wire()] = built_in()
    table = {
        "delta_legacy": FORMAT,
        "serial": serial,
        "issued": int(time.time()) if issued is None else issued,
        "wires": dict(sorted(wires.items())),
        "disabled_capabilities": list(previous.get("disabled_capabilities", [])) if previous else [],
    }
    if previous and "platform_policy" in previous:
        table["platform_policy"] = previous["platform_policy"]
    document = (json.dumps(table, indent=2) + "\n").encode()
    parse(document, wire())
    return document


# ---------- Ed25519, with openssl


def openssl() -> str:
    """an openssl command that does Ed25519 (OpenSSL 3; macOS's LibreSSL does not)"""
    candidates = [os.environ["OPENSSL"]] if os.environ.get("OPENSSL") else \
        ["openssl", "/opt/homebrew/opt/openssl@3/bin/openssl", "/usr/local/opt/openssl@3/bin/openssl"]
    for candidate in candidates:
        path = shutil.which(candidate)
        if not path:
            continue
        version = subprocess.run([path, "version"], capture_output=True, text=True).stdout
        if version.startswith("OpenSSL 3"):
            return path
    raise SystemExit("needs OpenSSL 3's openssl command (or $OPENSSL naming one)")


def run(arguments, **kwargs) -> bytes:
    return subprocess.run([openssl(), *arguments], check=True, capture_output=True, **kwargs).stdout


def public_key(key: Path) -> bytes:
    der = run(["pkey", "-in", str(key), "-pubout", "-outform", "DER"])
    if not der.startswith(ED25519_SPKI_PREFIX) or len(der) != len(ED25519_SPKI_PREFIX) + 32:
        raise SystemExit(f"{key} is not an Ed25519 key")
    return der[len(ED25519_SPKI_PREFIX):]


def keygen(out: Path) -> bytes:
    if out.exists():
        raise SystemExit(f"{out} exists: not replaced")
    descriptor = os.open(str(out), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    os.close(descriptor)
    run(["genpkey", "-algorithm", "ed25519", "-out", str(out)])
    os.chmod(out, 0o600)
    return public_key(out)


def c_array(key: bytes) -> str:
    return "\n".join("\t\t" + " ".join(f"0x{byte:02x}," for byte in half) for half in (key[:16], key[16:]))


def sign_bytes(key: Path, document: bytes) -> bytes:
    with tempfile.TemporaryDirectory() as directory:
        text = Path(directory) / "document"
        signature = Path(directory) / "signature"
        text.write_bytes(document)
        run(["pkeyutl", "-sign", "-inkey", str(key), "-rawin", "-in", str(text), "-out", str(signature)])
        raw = signature.read_bytes()
    if len(raw) != 64:
        raise SystemExit(f"openssl gave a {len(raw)}-byte signature")
    return raw


def verify_bytes(key: bytes, document: bytes, signature: bytes) -> bool:
    with tempfile.TemporaryDirectory() as directory:
        text = Path(directory) / "document"
        signature_file = Path(directory) / "signature"
        public = Path(directory) / "public.der"
        text.write_bytes(document)
        signature_file.write_bytes(signature)
        public.write_bytes(ED25519_SPKI_PREFIX + key)
        result = subprocess.run([openssl(), "pkeyutl", "-verify", "-pubin", "-keyform", "DER", "-inkey",
                                 str(public), "-rawin", "-in", str(text), "-sigfile", str(signature_file)],
                                capture_output=True)
    return result.returncode == 0


def signature_from_text(text: str) -> bytes:
    text = text.rstrip(" \t\r\n")
    if not re.fullmatch(r"[0-9a-fA-F]{128}", text):
        raise TableError("the signature is not 128 hex digits")
    return bytes.fromhex(text)


def header_keys():
    """delta_key.h's keys (the block before the test key's #else), but zeros"""
    text = KEY_HEADER.read_text().split("#else", 1)[0]
    keys = []
    for block in re.findall(r"\{([^{}]*)\}", text):
        values = re.findall(r"0x([0-9a-fA-F]{2})", block)
        if len(values) == 32:
            key = bytes(int(value, 16) for value in values)
            if any(key):
                keys.append(key)
    return keys


def check(document: bytes, signature_text: str, keys) -> dict:
    """the signed table, checked as the game does: size, signature, then the
    document (but not against a build's numbers); TableError if not"""
    if len(document) > DOCUMENT_SIZE:
        raise TableError(f"it is {len(document)} bytes, more than {DOCUMENT_SIZE}")
    signature = signature_from_text(signature_text)
    if not any(verify_bytes(key, document, signature) for key in keys):
        raise TableError("its signature does not match")
    return parse(document)


def signed_table(document: bytes, signature: bytes) -> bytes:
    """the table as the game caches it and passes it on: the signature's hex
    digits, a line feed, the document"""
    return signature.hex().encode() + b"\n" + document


# ---------- the commands


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("make")
    command.add_argument("--serial", type=int, required=True)
    command.add_argument("--issued", type=int)
    command.add_argument("--from", dest="previous", type=Path, help="a table whose other wires' rows to keep")
    command.add_argument("--out", type=Path)
    command = commands.add_parser("sign")
    command.add_argument("--key", type=Path, required=True)
    command.add_argument("document", type=Path)
    command = commands.add_parser("verify")
    command.add_argument("--public-key", help="a public key's 64 hex digits, in place of delta_key.h's")
    command.add_argument("document", type=Path)
    command = commands.add_parser("keygen")
    command.add_argument("--out", type=Path, required=True)
    arguments = parser.parse_args()

    try:
        if arguments.command == "make":
            previous = parse(arguments.previous.read_bytes()) if arguments.previous else None
            if previous and arguments.serial <= previous["serial"]:
                raise TableError(f"serial {arguments.serial} is not newer than {previous['serial']}")
            document = make(arguments.serial, arguments.issued, previous)
            if arguments.out:
                arguments.out.write_bytes(document)
            else:
                sys.stdout.write(document.decode())
        elif arguments.command == "sign":
            document = arguments.document.read_bytes()
            parse(document, wire())
            signature = sign_bytes(arguments.key, document)
            if not verify_bytes(public_key(arguments.key), document, signature):
                raise SystemExit("openssl's signature does not verify")
            Path(f"{arguments.document}.sig").write_text(signature.hex() + "\n")
            print(f"{arguments.document}.sig")
        elif arguments.command == "verify":
            keys = [bytes.fromhex(arguments.public_key)] if arguments.public_key else header_keys()
            if not keys or any(len(key) != 32 for key in keys):
                print(f"no key in delta_key.h: give --public-key", file=sys.stderr)
                return 1
            table = check(arguments.document.read_bytes(), Path(f"{arguments.document}.sig").read_text(), keys)
            row = table["wires"].get(wire())
            print(f"{arguments.document}: signed, serial {table['serial']}; {wire()}: {row or 'no row'}")
        else:
            key = keygen(arguments.out)
            print(f"private key: {arguments.out} (keep it out of every repository: a CI secret)")
            print(f"public key: {key.hex()}")
            print("for port/linux/src/delta_key.h:")
            print(c_array(key))
    except TableError as error:
        print(f"not a legacy table: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
