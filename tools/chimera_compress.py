#!/usr/bin/env python3
"""Compresses a Halo PC map (Custom Edition's, cache version 609, or Halo PC
retail's, 7) as Chimera's chimera-compress does, for the tests
(tools/test_compressed_maps.py) and for trying the game's reader
(port/linux/src/map_compressed.c) by hand:

    python3 -I tools/chimera_compress.py <map> <compressed map> [--frames N]

The format is Chimera's, by Snowy Mouse and its contributors (GPL-3.0,
github.com/SnowyMouse/chimera, compressor/main.cpp): the map's 2048-byte
header, cleaned as chimera-compress cleans it (only its signatures, version,
size, tag data's offset and size, name, build, type, checksum, flags and the
Lua fields kept), its size set to the file's and 0x861A0000 added to its
version; then the rest of the map as Zstandard data. chimera-compress
writes one frame (level 19); --frames splits it into several, which
Chimera's reader also reads.

Zstandard comes from the zstd program (zstd on the PATH, or $ZSTD).

SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

HEADER_SIZE = 0x800
COMPRESSED_MARK = 0x861A0000
HEAD = b"daeh"
FOOT = b"toof"


def zstd_program():
    """the zstd program's path, or None"""
    return os.environ.get("ZSTD") or shutil.which("zstd")


def clean_header(header: bytes, size: int) -> bytearray:
    """the header as chimera-compress writes it, before its version is
    marked: its known fields alone, its size the file's"""
    if len(header) != HEADER_SIZE or header[0:4] != HEAD or header[0x7FC:0x800] != FOOT:
        raise ValueError("not a Halo PC cache file")
    version = struct.unpack_from("<I", header, 0x4)[0]
    if version not in (7, 609):
        raise ValueError(f"cache version {version} is not Custom Edition's or Halo PC's")
    cleaned = bytearray(HEADER_SIZE)
    cleaned[0:4] = HEAD
    cleaned[0x7FC:0x800] = FOOT
    struct.pack_into("<I", cleaned, 0x4, version)
    struct.pack_into("<I", cleaned, 0x8, size)
    # (tag data's offset and size)
    cleaned[0x10:0x18] = header[0x10:0x18]
    # (name and build, each terminated within its 32 bytes)
    for offset in (0x20, 0x40):
        field = bytes(header[offset:offset + 32]).split(b"\0", 1)[0][:31]
        cleaned[offset:offset + len(field)] = field
    # (type, checksum and flags)
    cleaned[0x60:0x62] = header[0x60:0x62]
    cleaned[0x64:0x6C] = header[0x64:0x6C]
    # (the Lua fields)
    cleaned[0x310:0x318] = header[0x310:0x318]
    return cleaned


def zstd_compress(data: bytes, program: str, checksum: bool = False) -> bytes:
    result = subprocess.run([program, "-19", "-q", "-c", "--check" if checksum else "--no-check"], input=data,
                            capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.decode(errors="replace"))
    return result.stdout


def compress(data: bytes, frames: int = 1, program: str = None, checksum: bool = False) -> bytes:
    """the map's bytes compressed, as chimera-compress compresses them (with
    checksum, its frames checksummed, which chimera-compress's are not)"""
    program = program or zstd_program()
    if not program:
        raise RuntimeError("no zstd program")
    if len(data) <= HEADER_SIZE:
        raise ValueError("too short to be a map")
    header = clean_header(data[:HEADER_SIZE], len(data))
    struct.pack_into("<I", header, 0x4, struct.unpack_from("<I", header, 0x4)[0] | COMPRESSED_MARK)
    body = data[HEADER_SIZE:]
    step = max(1, -(-len(body) // max(1, frames)))
    parts = [zstd_compress(body[start:start + step], program, checksum) for start in range(0, len(body), step)]
    return bytes(header) + b"".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("map")
    parser.add_argument("output")
    parser.add_argument("--frames", type=int, default=1)
    arguments = parser.parse_args()
    with open(arguments.map, "rb") as file:
        data = file.read()
    compressed = compress(data, arguments.frames)
    with open(arguments.output, "wb") as file:
        file.write(compressed)
    print(f"{arguments.map}: {len(data)} bytes --> {len(compressed)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
