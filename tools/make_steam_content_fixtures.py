"""Writes the Steam content client's test fixtures
(port/linux/tests/steam_content/*.bin): a chunk packed as Steam's content
servers give it, once with LZMA ("VZa") and once with Zstandard ("VSZa"),
and a small manifest with encrypted file names, all with a test key
(bytes 0 to 31). Needs the Python packages cryptography and zstandard; the
fixtures are kept in the repository, so this runs only to change them.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
"""
import base64
import hashlib
import io
import lzma
import struct
import zipfile
import zlib
from pathlib import Path

import zstandard
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

OUT = Path(__file__).resolve().parent.parent / "port" / "linux" / "tests" / "steam_content"
KEY = bytes(range(32))
IV = bytes(range(100, 116))


def plain_bytes(size, seed):
    """text with some repetition and some noise, the same every run"""
    out = bytearray()
    state = seed
    while len(out) < size:
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        out += b"Blood Gulch %d " % (state % 97)
    return bytes(out[:size])


def encrypt(data):
    pad = 16 - len(data) % 16
    data += bytes([pad]) * pad
    head = Cipher(algorithms.AES(KEY), modes.ECB()).encryptor().update(IV)
    body = Cipher(algorithms.AES(KEY), modes.CBC(IV)).encryptor()
    return head + body.update(data) + body.finalize()


def vzip(plain):
    alone = lzma.compress(plain, format=lzma.FORMAT_ALONE)
    props, data = alone[:5], alone[13:]
    return b"VZa" + struct.pack("<I", 0x12345678) + props + data + struct.pack("<II", zlib.crc32(plain),
                                                                               len(plain)) + b"zv"


def vzstd(plain):
    frame = zstandard.ZstdCompressor(level=3).compress(plain)
    return b"VSZa" + struct.pack("<I", 0) + frame + struct.pack("<II", zlib.crc32(plain), len(plain)) + \
        b"\0\0\0\0" + b"zsv"


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def field(number, wire, value):
    key = varint(number << 3 | wire)
    if wire == 0:
        return key + varint(value)
    return key + varint(len(value)) + value


def manifest(files):
    payload = b""
    for name, size, flags, sha, chunks in files:
        mapping = field(1, 2, base64.b64encode(encrypt(name.encode())))
        mapping += field(2, 0, size) + field(3, 0, flags)
        if sha:
            mapping += field(5, 2, sha)
        for chunk_sha, offset, original, compressed in chunks:
            mapping += field(6, 2, field(1, 2, chunk_sha) + field(3, 0, offset) + field(4, 0, original) +
                             field(5, 0, compressed))
        payload += field(1, 2, mapping)
    metadata = field(1, 0, 976730) + field(4, 0, 1)
    raw = struct.pack("<II", 0x71F617D0, len(payload)) + payload
    raw += struct.pack("<II", 0x1F4812BE, len(metadata)) + metadata
    raw += struct.pack("<II", 0x1B81B817, 0) + struct.pack("<I", 0x32C415AB)
    blob = io.BytesIO()
    with zipfile.ZipFile(blob, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("z", raw)
    return blob.getvalue()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    first, second = plain_bytes(6000, 1), plain_bytes(3000, 2)
    (OUT / "chunk_lzma.bin").write_bytes(encrypt(vzip(first)))
    (OUT / "chunk_zstd.bin").write_bytes(encrypt(vzstd(second)))
    whole = first + second
    (OUT / "manifest.zip").write_bytes(manifest([
        ("maps", 0, 0x40, None, []),
        ("maps\\test.map", len(whole), 0, hashlib.sha1(whole).digest(), [
            (hashlib.sha1(first).digest(), 0, len(first), 0),
            (hashlib.sha1(second).digest(), len(first), len(second), 0)]),
    ]))
    print("sha1 first", hashlib.sha1(first).hexdigest(), "second", hashlib.sha1(second).hexdigest())


if __name__ == "__main__":
    main()
