"""The client_messages_44 seeds: a host's game settings record in pieces, as
client_messages_fuzz.c takes them (byte 0 the client's state, then each
message's flags byte and little-endian length), each piece the packet the
codec reads: its version byte, the record's total, the piece's offset and
length (big-endian, as the codec's shorts), its bytes, its type last. The
records are 13,200 bytes (OpenCE's 44), 13,244 (Content Evolved's, also
with its options' byte 0x2B at 1 and 2), 13,136 and 13,120.

    python3 tools/tests/host_fuzz/client_messages_44/make_seeds.py
"""
import hashlib
import struct
from pathlib import Path

PIECE = 0xE00
HEAD = 13088
SEEDS = Path(__file__).resolve().parent / "seeds"


def seed(total, state, option=None):
    record = bytearray(total)
    if option is not None:
        record[HEAD + 0x2B] = option
    data = bytearray([state])
    offset = 0
    while offset < total:
        length = min(PIECE, total - offset)
        piece = record[offset:offset + length] + bytes(PIECE - length)
        message = bytes([1]) + struct.pack(">HHH", total, offset, length) + piece + bytes([6])
        data += bytes([0]) + struct.pack("<H", len(message)) + message
        offset += length
    return bytes(data)


def main():
    seeds = [seed(total, state) for total in (13200, 13244, 13136, 13120) for state in (2, 3)]
    seeds += [seed(13244, 2, 1), seed(13244, 2, 2)]
    SEEDS.mkdir(exist_ok=True)
    for data in seeds:
        (SEEDS / hashlib.sha1(data).hexdigest()).write_bytes(data)


if __name__ == "__main__":
    main()
