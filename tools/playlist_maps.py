#!/usr/bin/env python3
"""Checks, signs and verifies a playlist's map set (docs/playlist-maps.md;
the game's side is port/linux/src/playlist_manifest.c).

    playlist_maps.py check maps.json
        the manifest read as the game reads it (no signature)
    playlist_maps.py sign --key KEY.pem maps.json
        writes maps.json.sig: Ed25519 over the context line and the
        document's exact bytes, with Delta's key (CI's DELTA_SIGNING_KEY)
    playlist_maps.py verify [--public-key HEX [--last-epoch N]] maps.json
        checks maps.json.sig against the key (or delta_key.h's keys) and the
        key's epoch, then the manifest

It uses delta_table.py's openssl helpers (OpenSSL 3, or the one $OPENSSL
names), so nothing beyond Python's own library is needed.
"""

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import delta_table  # noqa: E402

CONTEXT = b"chupathingy-playlist-maps 1\n"
FORMAT = 1
DOCUMENT_SIZE = 65536
MAXIMUM_FILES = 128
MAXIMUM_PACKS = 16
MAXIMUM_SIZE = 1024 * 1024 * 1024
SOURCES = ("halonet", "chupathingy", "own")

ID = re.compile(r"[a-z0-9_]{1,32}")
PACK_ID = re.compile(r"[a-z0-9_.-]{1,24}")
FILE = re.compile(r"(?!\.)(?!.*\.\.)[a-z0-9_.-]{1,36}\.(map|txt)")
FOLDER = re.compile(r"maps|maps_ce|maps_[a-z0-9]{1,16}")
PATH = re.compile(r"(?=.{1,160}$)[a-z0-9_-][a-z0-9_.-]*(/[a-z0-9_-][a-z0-9_.-]*)*")
MAP = re.compile(r"(?!\.)(?!.*\.\.)[a-z0-9_.-]+(@[a-z0-9]{1,16})?")
SHA256 = re.compile(r"[0-9a-f]{64}")


class ManifestError(ValueError):
    pass


def _no_duplicates(pairs):
    keys = [key for key, _ in pairs]
    if len(keys) != len(set(keys)):
        raise ManifestError("a field is there twice")
    return dict(pairs)


def _text(value, name, maximum=48, empty=False):
    if not isinstance(value, str) or len(value.encode()) > maximum or (not value and not empty) or \
            any(ord(c) < 0x20 or ord(c) == 0x7f for c in value):
        raise ManifestError(f"its {name} is wrong")
    return value


def _integer(value, minimum, maximum, name):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ManifestError(f"its {name} is wrong")
    return value


def parse(document: bytes) -> dict:
    """the manifest, checked as the game checks it; ManifestError if not"""
    if len(document) > DOCUMENT_SIZE:
        raise ManifestError(f"it is {len(document)} bytes, more than {DOCUMENT_SIZE}")
    try:
        manifest = json.loads(document.decode("utf-8"), object_pairs_hook=_no_duplicates)
    except (UnicodeDecodeError, json.JSONDecodeError) as problem:
        raise ManifestError(f"it is not JSON: {problem}") from None
    if not isinstance(manifest, dict):
        raise ManifestError("it is not a JSON object")
    for field in ("playlist_maps", "playlist", "name", "serial", "issued", "files"):
        if field not in manifest:
            raise ManifestError(f"it has no {field}")
    if manifest["playlist_maps"] != FORMAT or type(manifest["playlist_maps"]) is not int:
        raise ManifestError("its format is not one this tool reads")
    if not isinstance(manifest["playlist"], str) or not ID.fullmatch(manifest["playlist"]):
        raise ManifestError("its playlist is wrong")
    _text(manifest["name"], "name")
    _integer(manifest["serial"], 1, 0xFFFFFFFE, "serial")
    _integer(manifest["issued"], 0, 1 << 53, "issued")
    packs = manifest.get("packs", {})
    if not isinstance(packs, dict) or len(packs) > MAXIMUM_PACKS:
        raise ManifestError("its packs are wrong")
    for pack_id, pack in packs.items():
        if not PACK_ID.fullmatch(pack_id) or not isinstance(pack, dict) or "name" not in pack:
            raise ManifestError(f"pack {pack_id!r} is wrong")
        _text(pack["name"], "pack's name")
        for field, maximum in (("author", 48), ("credits", 255)):
            if field in pack:
                _text(pack[field], f"pack's {field}", maximum, empty=True)
        if "permission" in pack and not isinstance(pack["permission"], str):
            raise ManifestError("a pack's permission is wrong")
    files = manifest["files"]
    if not isinstance(files, list) or not 1 <= len(files) <= MAXIMUM_FILES:
        raise ManifestError("its files are wrong")
    places = set()
    for entry in files:
        if not isinstance(entry, dict):
            raise ManifestError("a file's entry is not an object")
        for field in ("folder", "file", "source"):
            if field not in entry:
                raise ManifestError(f"a file has no {field}")
        if not isinstance(entry["folder"], str) or not FOLDER.fullmatch(entry["folder"]):
            raise ManifestError("a file's folder is wrong")
        if not isinstance(entry["file"], str) or not FILE.fullmatch(entry["file"]):
            raise ManifestError("a file's file is wrong")
        if entry["source"] not in SOURCES:
            raise ManifestError("a file's source is not one this tool knows")
        if "map" in entry and (not isinstance(entry["map"], str) or len(entry["map"]) > 48 or
                               not MAP.fullmatch(entry["map"])):
            raise ManifestError("a file's map is wrong")
        if "title" in entry:
            _text(entry["title"], "file's title", empty=True)
        if "size" in entry:
            _integer(entry["size"], 1, MAXIMUM_SIZE, "file's size")
        if "sha256" in entry and (not isinstance(entry["sha256"], str) or not SHA256.fullmatch(entry["sha256"])):
            raise ManifestError("a file's sha256 is wrong")
        if "pack" in entry and (not isinstance(entry["pack"], str) or entry["pack"] not in packs):
            raise ManifestError("a file names a pack the manifest does not have")
        if entry["source"] != "own" and ("size" not in entry or "sha256" not in entry):
            raise ManifestError("a file to download has no size or SHA-256")
        if (entry["source"] == "chupathingy") != ("path" in entry):
            raise ManifestError("a path is for Chupathingy's files alone, and each has one")
        if "path" in entry and (not isinstance(entry["path"], str) or not PATH.fullmatch(entry["path"])):
            raise ManifestError("a file's path is wrong")
        if entry["source"] == "own" and "pack" not in entry:
            raise ManifestError("a file of the player's own has no pack")
        if entry["source"] != "own" and entry["folder"] == "maps":
            raise ManifestError("nothing is downloaded into maps")
        if entry["source"] == "halonet" and entry["folder"] != "maps_ce":
            raise ManifestError("the community repositories' maps go in maps_ce")
        place = (entry["folder"], entry["file"])
        if place in places:
            raise ManifestError("a file is there twice")
        places.add(place)
    return manifest


def check(document: bytes, signature_text: str, keys, last_epochs=None) -> dict:
    """the signed manifest checked as the game does: its signature over the
    context and the document, the key's epoch, then the manifest"""
    if len(document) > DOCUMENT_SIZE:
        raise ManifestError(f"it is {len(document)} bytes, more than {DOCUMENT_SIZE}")
    signature = delta_table.signature_from_text(signature_text)
    signer = next((index for index, key in enumerate(keys)
                   if delta_table.verify_bytes(key, CONTEXT + document, signature)), None)
    if signer is None:
        raise ManifestError("its signature does not match")
    manifest = parse(document)
    last = (last_epochs or [255] * len(keys))[signer]
    if delta_table.epoch(manifest["serial"]) > last:
        raise ManifestError(f"its epoch is past its key's last ({last})")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("check")
    command.add_argument("manifest", type=Path)
    command = commands.add_parser("sign")
    command.add_argument("--key", type=Path, required=True)
    command.add_argument("manifest", type=Path)
    command = commands.add_parser("verify")
    command.add_argument("--public-key")
    command.add_argument("--last-epoch", type=int)
    command.add_argument("manifest", type=Path)
    arguments = parser.parse_args()
    document = arguments.manifest.read_bytes()
    try:
        if arguments.command == "check":
            manifest = parse(document)
            print(f"{arguments.manifest}: {manifest['playlist']}, serial {manifest['serial']}, "
                  f"{len(manifest['files'])} files")
        elif arguments.command == "sign":
            parse(document)
            signature = delta_table.sign_bytes(arguments.key, CONTEXT + document)
            if not delta_table.verify_bytes(delta_table.public_key(arguments.key), CONTEXT + document, signature):
                raise SystemExit("openssl's signature does not verify")
            Path(str(arguments.manifest) + ".sig").write_text(signature.hex() + "\n")
            print(f"signed {arguments.manifest}")
        else:
            if arguments.public_key:
                keys = [bytes.fromhex(arguments.public_key)]
                last_epochs = [arguments.last_epoch if arguments.last_epoch is not None else 255]
            else:
                keys, last_epochs = delta_table.header_keys(), delta_table.header_last_epochs()
            signature = Path(str(arguments.manifest) + ".sig").read_text()
            manifest = check(document, signature, keys, last_epochs)
            print(f"{arguments.manifest}: verified, {manifest['playlist']} serial {manifest['serial']}")
    except (ManifestError, delta_table.TableError) as problem:
        print(f"{arguments.manifest}: {problem}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
