#!/usr/bin/env python3
"""Signs a release's update zips for the self-updater, and checks them.

The game (port/linux/src/updater.c, update_signature.c) installs an update
only when <zip>.sig, beside the zip in the release, is an Ed25519 signature,
by the key whose public half it is built with (port/linux/src/update_key.h),
of this text:

    chupathingyce-update-signature 1
    asset <the zip's name>
    version <the release's version, without the v>
    size <the zip's size in bytes>
    sha512 <the zip's SHA-512, lowercase hex>

The .sig holds the signature as 128 hex digits and a line feed.

It uses the openssl command (OpenSSL 3, or the one $OPENSSL names) for
Ed25519, so nothing beyond Python's own library is needed:

    update_sign.py sign <key.pem> <version> <zip>...     writes each <zip>.sig
    update_sign.py verify [--public <hex>] <version> <zip>...
                                                         checks them against update_key.h's keys
    update_sign.py public <key.pem>                      the key's public half, for update_key.h

A key is made with: openssl genpkey -algorithm ed25519 -out key.pem
(keep it out of every repository: the release workflow has it as a secret).
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

KEY_HEADER = Path(__file__).resolve().parent.parent / "port/linux/src/update_key.h"
WORD = re.compile(r"^[A-Za-z0-9._+-]{1,127}$")
# (an Ed25519 public key's DER: this prefix, then its 32 bytes)
ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")


def openssl() -> str:
    return os.environ.get("OPENSSL", "openssl")


def message(asset: str, version: str, data: bytes) -> bytes:
    if not WORD.match(asset) or not WORD.match(version):
        raise SystemExit(f"not an asset name or version: {asset!r} {version!r}")
    return (f"chupathingyce-update-signature 1\nasset {asset}\nversion {version}\n"
            f"size {len(data)}\nsha512 {hashlib.sha512(data).hexdigest()}\n").encode()


def run(arguments, **kwargs) -> bytes:
    return subprocess.run([openssl(), *arguments], check=True, capture_output=True, **kwargs).stdout


def sign(key: Path, version: str, zips) -> None:
    with tempfile.TemporaryDirectory() as directory:
        for zip_path in map(Path, zips):
            text = Path(directory) / "message"
            signature = Path(directory) / "signature"
            text.write_bytes(message(zip_path.name, version, zip_path.read_bytes()))
            run(["pkeyutl", "-sign", "-inkey", str(key), "-rawin", "-in", str(text), "-out", str(signature)])
            raw = signature.read_bytes()
            if len(raw) != 64:
                raise SystemExit(f"{zip_path}: openssl gave a {len(raw)}-byte signature")
            Path(f"{zip_path}.sig").write_text(raw.hex() + "\n")
            print(f"{zip_path}.sig")


def header_keys():
    text = KEY_HEADER.read_text()
    # (the release keys: the block before the test key's #else)
    text = text.split("#else", 1)[0]
    keys = []
    for block in re.findall(r"\{([^{}]*)\}", text):
        values = re.findall(r"0x([0-9a-fA-F]{2})", block)
        if len(values) == 32:
            key = bytes(int(value, 16) for value in values)
            if any(key):
                keys.append(key)
    return keys


def verify(version: str, zips, keys=None) -> int:
    keys = keys if keys is not None else header_keys()
    if not keys:
        print(f"no key in {KEY_HEADER}: nothing to check against", file=sys.stderr)
        return 1
    failed = 0
    with tempfile.TemporaryDirectory() as directory:
        for zip_path in map(Path, zips):
            text = Path(directory) / "message"
            signature = Path(directory) / "signature"
            text.write_bytes(message(zip_path.name, version, zip_path.read_bytes()))
            signature.write_bytes(bytes.fromhex(Path(f"{zip_path}.sig").read_text().strip()))
            good = False
            for key in keys:
                public = Path(directory) / "public.der"
                public.write_bytes(ED25519_SPKI_PREFIX + key)
                result = subprocess.run([openssl(), "pkeyutl", "-verify", "-pubin", "-keyform", "DER", "-inkey",
                                         str(public), "-rawin", "-in", str(text), "-sigfile", str(signature)],
                                        capture_output=True)
                good = good or result.returncode == 0
            print(f"{zip_path}: {'signed' if good else 'NOT SIGNED by update_key.h'}")
            failed += not good
    return 1 if failed else 0


def public(key: Path) -> None:
    der = run(["pkey", "-in", str(key), "-pubout", "-outform", "DER"])
    if not der.startswith(ED25519_SPKI_PREFIX) or len(der) != len(ED25519_SPKI_PREFIX) + 32:
        raise SystemExit("not an Ed25519 key")
    raw = der[len(ED25519_SPKI_PREFIX):]
    print(raw.hex())
    for half in (raw[:16], raw[16:]):
        print("\t\t" + " ".join(f"0x{byte:02x}," for byte in half))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("sign")
    command.add_argument("key", type=Path)
    command.add_argument("version")
    command.add_argument("zips", nargs="+")
    command = commands.add_parser("verify")
    command.add_argument("version")
    command.add_argument("zips", nargs="+")
    command.add_argument("--public", help="a public key's 64 hex digits, in place of update_key.h's")
    command = commands.add_parser("public")
    command.add_argument("key", type=Path)
    arguments = parser.parse_args()
    if arguments.command == "sign":
        sign(arguments.key, arguments.version, arguments.zips)
    elif arguments.command == "verify":
        keys = [bytes.fromhex(arguments.public)] if arguments.public else None
        return verify(arguments.version, arguments.zips, keys)
    else:
        public(arguments.key)
    return 0


if __name__ == "__main__":
    sys.exit(main())
