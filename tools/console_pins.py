#!/usr/bin/env python3
"""Makes, signs and checks the console pin document (docs/console-https.md,
"Pin updates"; the game's side is port/console/https/console_pins.c).

The document, console-pins.json, carries the SHA-256 pins of the site's key
(its certificate's SubjectPublicKeyInfo):

    {"console_pins": 1, "serial": 3, "epoch": 0, "issued": 1793232000,
     "hosts": {"stats.chupathingy.org": {"current": "<hex>", "next": "<hex>"}}}

and its signature, console-pins.json.sig, the Ed25519 signature of its exact
bytes by a console pin key (port/console/https/console_pins_key.h), as 128 hex
digits and a line feed. A serial's top byte is its epoch: the primary key
signs epoch 0 alone, the recovery key any. At most 2048 bytes.

    console_pins.py make --serial N --pin HOST=CURRENT[,NEXT] [...] [--issued T] [--out FILE]
    console_pins.py sign --key KEY.pem console-pins.json        writes console-pins.json.sig
    console_pins.py verify [--public-key HEX [--last-epoch N]] console-pins.json
        checks the signature (by default against console_pins_key.h's keys)
        and the epoch, and prints the pins
    console_pins.py pin (--certificate FILE | --host HOST)
        a certificate's pin: a PEM file's, or the one HOST:443 serves
    console_pins.py next-serial [--from console-pins.json] [--new-epoch]
    console_pins.py keygen --out KEY.pem
        a new pin key pair: the private key into KEY.pem (yours alone, kept
        offline or as a CI secret, never in a repository), the public half
        printed as hex and as console_pins_key.h's C array

Ed25519 is the openssl command's (OpenSSL 3, or the one $OPENSSL names), as
tools/delta_table.py uses it.
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import delta_table  # noqa: E402  (its openssl Ed25519)

ROOT = Path(__file__).resolve().parent.parent
KEY_HEADER = ROOT / "port/console/https/console_pins_key.h"
FORMAT = 1
DOCUMENT_SIZE = 2048
MAXIMUM_PINS = 4
LAST_EPOCHS = [0, 255]
PIN = re.compile(r"[0-9a-f]{64}")


class PinsError(Exception):
    pass


def epoch(serial: int) -> int:
    return serial >> 24


def make(serial: int, hosts: dict, issued: int) -> bytes:
    if not 1 <= serial <= 0xFFFFFFFF:
        raise PinsError("the serial is from 1 to 2^32 - 1")
    entries = {}
    for host, pins in hosts.items():
        if not pins or len(pins) > 2 or not all(PIN.fullmatch(pin) for pin in pins):
            raise PinsError(f"{host}: give a current pin and at most a next one, 64 hex digits each")
        entries[host] = {"current": pins[0], **({"next": pins[1]} if len(pins) > 1 else {})}
    document = {"console_pins": FORMAT, "serial": serial, "epoch": epoch(serial), "issued": issued,
                "hosts": entries}
    data = (json.dumps(document, indent=1, sort_keys=False) + "\n").encode()
    if len(data) > DOCUMENT_SIZE:
        raise PinsError(f"it is {len(data)} bytes, more than {DOCUMENT_SIZE}")
    return data


def parse(data: bytes) -> dict:
    """the document, checked as the game reads it"""
    if len(data) > DOCUMENT_SIZE:
        raise PinsError("it is too large")
    try:
        document = json.loads(data)
    except ValueError as problem:
        raise PinsError(f"it is not JSON: {problem}")
    if not isinstance(document, dict) or document.get("console_pins") != FORMAT:
        raise PinsError("it is not a format 1 pin document")
    serial = document.get("serial")
    if not isinstance(serial, int) or not 1 <= serial <= 0xFFFFFFFF:
        raise PinsError("its serial is not from 1 to 2^32 - 1")
    if document.get("epoch") != epoch(serial):
        raise PinsError("its epoch is not its serial's")
    if "\\" in data.decode("utf-8", "replace"):
        raise PinsError("it has an escape, which the game does not read")
    for host, entry in document.get("hosts", {}).items():
        pins = [entry.get(name) for name in ("current", "next") if name in entry]
        if "current" not in entry or not all(isinstance(pin, str) and PIN.fullmatch(pin) for pin in pins):
            raise PinsError(f"{host}: its pins are not 64 lowercase hex digits")
    return document


def header_keys():
    text = KEY_HEADER.read_text().split("#else", 1)[0]
    keys = []
    for block in re.findall(r"\{([^{}]*)\}", text):
        values = re.findall(r"0x([0-9a-fA-F]{2})", block)
        if len(values) == 32:
            key = bytes(int(value, 16) for value in values)
            if any(key):
                keys.append((key, LAST_EPOCHS[min(len(keys), len(LAST_EPOCHS) - 1)]))
    return keys


def check(data: bytes, signature_text: str, keys) -> dict:
    """the signed document, checked as the game does; PinsError if not"""
    if len(data) > DOCUMENT_SIZE:
        raise PinsError("it is too large")
    try:
        signature = delta_table.signature_from_text(signature_text)
    except delta_table.TableError as problem:
        raise PinsError(str(problem))
    signer = next(((key, last) for key, last in keys if delta_table.verify_bytes(key, data, signature)), None)
    if signer is None:
        raise PinsError("its signature does not match")
    document = parse(data)
    if epoch(document["serial"]) > signer[1]:
        raise PinsError(f"its epoch ({epoch(document['serial'])}) is past its key's last ({signer[1]})")
    return document


def pin_of_certificate(pem: bytes) -> str:
    public = subprocess.run([delta_table.openssl(), "x509", "-noout", "-pubkey"], input=pem, capture_output=True,
                            check=True).stdout
    der = subprocess.run([delta_table.openssl(), "pkey", "-pubin", "-outform", "DER"], input=public,
                         capture_output=True, check=True).stdout
    return hashlib.sha256(der).hexdigest()


def certificate_of_host(host: str) -> bytes:
    served = subprocess.run([delta_table.openssl(), "s_client", "-connect", f"{host}:443", "-servername", host],
                            input=b"", capture_output=True, timeout=30).stdout
    match = re.search(rb"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", served, re.S)
    if not match:
        raise PinsError(f"{host} gave no certificate")
    return match.group(0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    making = commands.add_parser("make")
    making.add_argument("--serial", type=int, required=True)
    making.add_argument("--pin", action="append", required=True, metavar="HOST=CURRENT[,NEXT]")
    making.add_argument("--issued", type=int, default=None)
    making.add_argument("--out", type=Path, default=Path("console-pins.json"))
    signing = commands.add_parser("sign")
    signing.add_argument("--key", type=Path, required=True)
    signing.add_argument("document", type=Path)
    verifying = commands.add_parser("verify")
    verifying.add_argument("--public-key")
    verifying.add_argument("--last-epoch", type=int, default=255)
    verifying.add_argument("document", type=Path)
    pinning = commands.add_parser("pin")
    source = pinning.add_mutually_exclusive_group(required=True)
    source.add_argument("--certificate", type=Path)
    source.add_argument("--host")
    serials = commands.add_parser("next-serial")
    serials.add_argument("--from", dest="previous", type=Path)
    serials.add_argument("--new-epoch", action="store_true")
    keys = commands.add_parser("keygen")
    keys.add_argument("--out", type=Path, required=True)
    arguments = parser.parse_args()

    try:
        if arguments.command == "make":
            hosts = {}
            for given in arguments.pin:
                host, _, pins = given.partition("=")
                hosts[host] = [pin.strip().lower() for pin in pins.split(",") if pin.strip()]
            data = make(arguments.serial, hosts, arguments.issued if arguments.issued is not None else int(time.time()))
            arguments.out.write_bytes(data)
            print(f"{arguments.out}: serial {arguments.serial}, {len(data)} bytes")
        elif arguments.command == "sign":
            data = arguments.document.read_bytes()
            parse(data)
            signature = delta_table.sign_bytes(arguments.key, data)
            Path(f"{arguments.document}.sig").write_text(signature.hex() + "\n")
            print(f"{arguments.document}.sig written")
        elif arguments.command == "verify":
            key_list = [(bytes.fromhex(arguments.public_key), arguments.last_epoch)] if arguments.public_key \
                else header_keys()
            if not key_list:
                raise PinsError("no key: console_pins_key.h's are zero (give --public-key)")
            document = check(arguments.document.read_bytes(), Path(f"{arguments.document}.sig").read_text(), key_list)
            print(f"ok: serial {document['serial']}, epoch {document['epoch']}")
            for host, entry in document.get("hosts", {}).items():
                print(f"  {host}: current {entry['current']}" + (f", next {entry['next']}" if "next" in entry else ""))
        elif arguments.command == "pin":
            pem = arguments.certificate.read_bytes() if arguments.certificate else certificate_of_host(arguments.host)
            print(pin_of_certificate(pem))
        elif arguments.command == "next-serial":
            previous = json.loads(arguments.previous.read_bytes())["serial"] if arguments.previous and \
                arguments.previous.is_file() else 0
            if arguments.new_epoch:
                if epoch(previous) >= 255:
                    raise PinsError("no epoch after 255")
                print(((epoch(previous) + 1) << 24) | 1)
            else:
                if epoch(previous + 1) != epoch(previous):
                    raise PinsError(f"serial {previous} is its epoch's last: a new epoch needs the recovery key")
                print(previous + 1)
        elif arguments.command == "keygen":
            public = delta_table.keygen(arguments.out)
            print(f"private key: {arguments.out} (keep it offline or as a CI secret, never in a repository)")
            print(f"public key: {public.hex()}")
            print("console_pins_key.h:\n\t{\n" + delta_table.c_array(public) + "\n\t},")
    except PinsError as problem:
        print(f"console_pins.py: {problem}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
