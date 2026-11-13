"""Tests the dedicated server's answer to Halo Custom Edition 1.0.10's join
wire (server/platform/ce_transport.c) through server/tests/gamespy_harness.c
on the loopback address: a stand-in client written here from the same
description (the transport's handshake, its key agreement, the cipher and
the message framing) connects, is told the server's map, asks to join and
is refused cleanly. The known values come from our own captures of Custom
Edition 1.0.10's client and server.
"""
import random
import socket
import struct
import sys
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools.test_gamespy_listing import Server, harness  # noqa: E402,F401

KEY = b"3b8dd8995f7c40a9a5c5b7dd5b481341"
MODULUS = 65537
MASK = 0xFFFFFFFF


def challenge_valid(c):
    first, parity = c[0], 0
    for i in range(1, 32):
        parity ^= ((c[i - 1] ^ first) ^ i) & 1 ^ (c[i - 1] < first) ^ (first < 0x4F)
        if (c[i] & 1) != parity:
            return False
    return True


def new_challenge(rng):
    c = [rng.randrange(93) + 0x21]
    parity = 0
    for i in range(1, 32):
        parity ^= ((c[i - 1] ^ c[0]) ^ i) & 1 ^ (c[i - 1] < c[0]) ^ (c[0] < 0x4F)
        ch = rng.randrange(93) + 0x21
        if (ch & 1) != parity:
            ch += 1
        c.append(ch)
    return bytes(c)


def response(c):
    out = bytearray(b"!" * 32)
    for i in range(32):
        if i in (0, 13):
            continue
        earlier = c[i] if i in (1, 14) else c[i - 1]
        picked = (KEY[(c[i] + i) % 32] + c[i] * i) & 31
        out[i] = abs(c[picked] ^ KEY[(earlier * i * 0x4647) % 32]) % 93 + 0x21
    return bytes(out)


def matches(expected, got):
    return all(expected[i] == got[i] for i in range(32) if i not in (0, 13))


def tea_key(secret):
    return struct.unpack("<4I", secret.to_bytes(16, "big"))


def encipher_block(k, block):
    v0, v1 = struct.unpack("<2I", block)
    s = 0
    for _ in range(32):
        s = (s + 0x9E3779B9) & MASK
        v0 = (v0 + ((((v1 << 4) + k[0]) & MASK) ^ ((v1 + s) & MASK) ^ (((v1 >> 5) + k[1]) & MASK))) & MASK
        v1 = (v1 + ((((v0 << 4) + k[2]) & MASK) ^ ((v0 + s) & MASK) ^ (((v0 >> 5) + k[3]) & MASK))) & MASK
    return struct.pack("<2I", v0, v1)


def decipher_block(k, block):
    v0, v1 = struct.unpack("<2I", block)
    s = 0xC6EF3720
    for _ in range(32):
        v1 = (v1 - ((((v0 << 4) + k[2]) & MASK) ^ ((v0 + s) & MASK) ^ (((v0 >> 5) + k[3]) & MASK))) & MASK
        v0 = (v0 - ((((v1 << 4) + k[0]) & MASK) ^ ((v1 + s) & MASK) ^ (((v1 >> 5) + k[1]) & MASK))) & MASK
        s = (s - 0x9E3779B9) & MASK
    return struct.pack("<2I", v0, v1)


def crc(data):
    return (~zlib.crc32(data)) & MASK


def seal(k, payload):
    b = bytearray(payload + struct.pack("<I", crc(payload)))
    n = len(b)
    if n >= 8:
        for i in range(0, n // 8 * 8, 8):
            b[i:i + 8] = encipher_block(k, bytes(b[i:i + 8]))
        if n % 8:
            b[n - 8:] = encipher_block(k, bytes(b[n - 8:]))
    return bytes(b)


def open_sealed(k, data):
    b = bytearray(data)
    n = len(b)
    if n >= 8:
        if n % 8:
            b[n - 8:] = decipher_block(k, bytes(b[n - 8:]))
        for i in range(0, n // 8 * 8, 8):
            b[i:i + 8] = decipher_block(k, bytes(b[i:i + 8]))
    assert struct.unpack("<I", b[-4:])[0] == crc(bytes(b[:-4])), "CRC"
    return bytes(b[:-4])


def frame(messages):
    bits = []

    def put(value, count):
        bits.extend((value >> i) & 1 for i in range(count))
    put(0, 11)
    for m in messages:
        put(0, 1)
        put(0xC | ((len(m) + 2) << 4), 16)
        for byte in m:
            put(byte, 8)
    total = (len(bits) + 7) // 8
    bits[0:11] = [(total >> i) & 1 for i in range(11)]
    bits += [0] * (total * 8 - len(bits))
    return bytes(sum(bits[i * 8 + j] << j for j in range(8)) for i in range(total))


def unframe(payload):
    pos = 0

    def get(count):
        nonlocal pos
        v = sum(((payload[(pos + i) >> 3] >> ((pos + i) & 7)) & 1) << i for i in range(count))
        pos += count
        return v
    assert get(11) == len(payload)
    out = []
    while len(payload) * 8 - pos >= 17:
        if get(1):
            break
        size = (get(16) >> 4) - 2
        out.append(bytes(get(8) for _ in range(size)))
    return out


def join_request(name="Tester", password=""):
    def wide(text, count):
        return b"".join(struct.pack(">H", ord(c)) for c in text).ljust(count * 2, b"\0")
    m = b"\x01" + b"message in a bot" + wide(password, 9)
    m += (b"0123456789abcdef0123456789abcdef" + b"7" * 40).ljust(73, b"\0")
    m += b"\x01" + wide(name, 12) + b"\xff\xff\xff\xff" + b"\x00\x00\xff\xff" + struct.pack(">I", 0x12345678) + b"\x0e"
    assert len(m) == 146
    return m


class Client:
    def __init__(self, port):
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(2.0)
        self.rng = random.Random(7)
        self.key = None
        self.out_sequence = 2
        self.in_sequence = 2

    def send(self, data):
        self.sock.sendto(data, ("127.0.0.1", self.port))

    def receive(self):
        return self.sock.recvfrom(4096)[0]

    def handshake(self):
        mine = new_challenge(self.rng)
        assert challenge_valid(mine)
        self.send(b"\xfe\xfe\x01\x00\x00\x00\x00" + mine)
        reply = self.receive()
        assert reply[:7] == b"\xfe\xfe\x02\x00\x00\x00\x01" and len(reply) == 71
        assert matches(response(mine), reply[7:39])
        theirs = reply[39:71]
        assert challenge_valid(theirs)
        exponent = self.rng.randrange(2, MODULUS - 1)
        public = pow(3, exponent, MODULUS)
        self.send(b"\xfe\xfe\x03\x00\x01\x00\x01" + response(theirs) + public.to_bytes(16, "big") +
                  struct.pack("<I", 616999))
        accept = self.receive()
        assert accept[:7] == b"\xfe\xfe\x04\x00\x01\x00\x02" and len(accept) == 23
        server_public = int.from_bytes(accept[7:23], "big")
        self.key = tea_key(pow(server_public, exponent, MODULUS))

    def reliable(self):
        """the next reliable message from the server (acknowledgements skipped)"""
        while True:
            packet = self.receive()
            if packet[:3] == b"\xfe\xfe\x00":
                sequence, = struct.unpack(">H", packet[3:5])
                assert sequence == self.in_sequence
                self.in_sequence += 1
                return unframe(open_sealed(self.key, packet[7:]))
            if packet[:3] == b"\xfe\xfe\x68":
                return None

    def send_messages(self, messages):
        payload = seal(self.key, frame(messages))
        self.send(b"\xfe\xfe\x00" + struct.pack(">HH", self.out_sequence, self.in_sequence) + payload)
        self.out_sequence += 1


def test_cipher_matches_capture():
    # (from our capture of Custom Edition 1.0.10's own server: the key the
    # exchange agreed, and its first reliable payload, the new client challenge)
    key = tea_key(0x3DDB)
    sealed = bytes.fromhex("4415bc6d7582ea0930f02cae716bc99af1f54237ebd407f70399a0e1914401f787b5387c3d43")
    plain = open_sealed(key, sealed)
    assert plain.hex() == "22c02010402787568697160700000000000000000020c6f6f6467656c73686064000"
    assert unframe(plain) == [bytes.fromhex("017472686578697100000000000000000000626c6f6f6467756c636800") + b"\x04"]
    assert frame(unframe(plain)) == plain
    assert seal(key, plain) == sealed


# (our capture of Custom Edition 1.0.10's server answering a join: the game
# settings delta, a test server's, then begin game)
CAPTURED_SETTINGS = (
    "54293ca0018c019401d0019401cc01d001000000002888b1bdbd919dd5b18da1992900368030803c8032003900010000"
    "00000000030100000000000000000000000000005802000000000000000000fe64000000000000000049920000499200"
    "201c00000400000000006504000808000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000084010001000604e006f006f0064006c0065000000ffff00000000"
    "f0ffffffffffffff0ffffffffffffffffff0ffffffffffffff0ffffffffffffffffff0ffffffffffffff0fffffffffff"
    "fffffff0ffffffffffffff0ffffffffffffffffff0ffffffffffffff0ffffffffffffffffff0ffffffffffffff0fffff"
    "fffffffffffff0ffffffffffffff0ffffffffffffffffff0ffffffffffffff9fa00c0010000000100000008011200000"
    "00004001")


def test_game_settings_match_capture(harness):  # noqa: F811
    import subprocess
    out = subprocess.run([str(harness), "settings"], capture_output=True, text=True, check=True).stdout.strip()
    assert out == CAPTURED_SETTINGS


# (our capture of Custom Edition 1.0.10's server spawning a player: player
# added, its weapon, its biped and the spawn, the four messages a client
# needs to be in the game)
CAPTURED_SPAWN = (
    "a2e800000080630f0000000081d20546e731feff010000000080ffff0300200000000080000000000000000000000000"
    "80000000000000000040dd24c081b80c008000e0fffffc940900000000000000f0070000f0070000f0070000f0070000"
    "f0070000f0070000f0070000f0070000f0070000f0070000f0070000f0c71f10d89c2a946e1b32fce6d0dd13782524c8"
    "0000000000f0ff1f03000000000000000000")


def test_spawn_matches_capture(harness):  # noqa: F811
    import subprocess
    out = subprocess.run([str(harness), "spawn"], capture_output=True, text=True, check=True).stdout.strip()
    assert out == CAPTURED_SPAWN


def test_client_updates_from_capture(harness):  # noqa: F811
    """A 1.0.10 client's updates (our capture: idle, walking forward, a jump,
    turning) read in order, each leaving out what didn't change."""
    import subprocess
    captured = ["0cb8290ca4da844f9fffff69", "06b889600f6c", "07b89970090160", "07b81980110042", "09b8a1d0a92a53f7a0"]
    lines = subprocess.run([str(harness), "updates", *captured], capture_output=True, text=True,
                           check=True).stdout.splitlines()
    assert lines[0] == ("read 1 id 0 ticks 9 flags 0 yaw 0.3106 pitch 0.0000 throttle 0 trigger 0 weapon 0 grenade 3 "
                        "medium 65535")
    assert "ticks 3 flags 0" in lines[1] and "throttle 8" in lines[1]
    assert "ticks 2 flags 2" in lines[2] and "throttle 0" in lines[2]
    assert "ticks 4 flags 0" in lines[3]
    assert "yaw 0.1039" in lines[4] and "throttle 8" in lines[4]


def update_payload(yaw, pitch):
    """a client's update (every field present) as the bytes of a payload"""
    bits = []

    def put(value, count):
        bits.extend((value >> i) & 1 for i in range(count))
    put(0, 11)
    put(1, 1)
    put(0, 1)
    put(13, 6)
    put(5, 6)
    put(3, 5)
    put(0b1010101010, 10)
    put(struct.unpack("<I", struct.pack("<f", yaw))[0], 32)
    put(struct.unpack("<I", struct.pack("<f", pitch))[0], 32)
    put(8, 4)
    put(0, 1)
    put(0, 3)
    put(0, 2)
    put(0, 16)
    total = (len(bits) + 7) // 8
    bits[0:11] = [(total >> i) & 1 for i in range(11)]
    bits += [0] * (total * 8 - len(bits))
    return bytes(sum(bits[i * 8 + j] << j for j in range(8)) for i in range(total)).hex()


def test_impossible_facing_is_refused(harness):  # noqa: F811
    """An update whose yaw or pitch is not a number the game can turn by is
    refused whole, and leaves the last update as it was."""
    import subprocess
    sane = update_payload(1.5, -0.25)
    for bad in (float("nan"), float("inf"), -float("inf"), 3.0e38):
        for yaw, pitch in ((bad, 0.0), (0.0, bad)):
            lines = subprocess.run([str(harness), "updates", sane, update_payload(yaw, pitch)],
                                   capture_output=True, text=True, check=True).stdout.splitlines()
            assert lines[0].startswith("read 1 id 5 ticks 3") and "yaw 1.5000 pitch -0.2500" in lines[0]
            assert lines[1].startswith("read 0 id 5 ticks 3") and "yaw 1.5000 pitch -0.2500" in lines[1]


def test_directions(harness):  # noqa: F811
    import subprocess
    lines = subprocess.run([str(harness), "direction"], capture_output=True, text=True, check=True).stdout.split()
    # (forward along x, as Custom Edition's server wrote a weapon's; straight up; along y)
    assert lines[0:2] == ["524288", "262144"]
    assert lines[2] == "0"
    assert lines[4:6] == ["524288", "524288"]


def test_keep_alive_matches_capture(harness):  # noqa: F811
    import subprocess
    out = subprocess.run([str(harness), "keepalive"], capture_output=True, text=True, check=True).stdout.strip()
    # (the first keep-alive Custom Edition 1.0.10's server sent after a join)
    assert out == "08a8060000000000"


def bits_of(hex_text):
    """A payload's bits, least significant first (the wire's order)."""
    return "".join(format(byte, "08b")[::-1] for byte in bytes.fromhex(hex_text))


# (our capture of Custom Edition 1.0.10's server: a client's chat line as
# the server passes it on, kind 0 (everyone), player 0; the last byte's
# unused bits are whatever the server's buffer held)
CAPTURED_CHAT = "22e80100704003280360036003780300012803b00328039003c80378037003280398"


def test_chat_matches_capture(harness):  # noqa: F811
    import subprocess
    out = subprocess.run([str(harness), "chat", "0", "0", "hello everyone"], capture_output=True, text=True,
                         check=True).stdout.strip()
    used = 11 + 8 + 16 + 8 + 14 * 16
    assert len(out) == len(CAPTURED_CHAT)
    assert bits_of(out)[:used] == bits_of(CAPTURED_CHAT)[:used]


def test_server_chat_line(harness):  # noqa: F811
    """A server line (kind 3, no player: as Custom Edition's sv_say sends
    it), read back."""
    import subprocess
    out = subprocess.run([str(harness), "chat", "3", "255", "Welcome!"], capture_output=True, text=True,
                         check=True).stdout.strip()
    bits = bits_of(out)
    number = lambda start, width: int(bits[start:start + width][::-1], 2)  # noqa: E731
    assert number(0, 11) == len(out) // 2
    assert number(11, 1) == 1 and number(12, 1) == 0 and number(13, 6) == 15
    assert number(19, 8) == 3 and number(27, 8) == 255 and number(35, 8) == 8
    assert "".join(chr(number(43 + 16 * index, 16)) for index in range(8)) == "Welcome!"


def test_unit_killed_fields(harness):  # noqa: F811
    """A death as Custom Edition 1.0.10's server writes one with no
    direction (its unit death message's definition read from the retail
    binary): the object, seven flags, a 16-bit part, a real, two reals of
    direction and the 32-bit respawn countdown."""
    import struct
    import subprocess
    out = subprocess.run([str(harness), "killed", "50", "150"], capture_output=True, text=True,
                         check=True).stdout.strip()
    bits = bits_of(out)
    number = lambda start, width: int(bits[start:start + width][::-1], 2)  # noqa: E731
    assert number(0, 11) == len(out) // 2
    assert number(11, 1) == 1 and number(12, 1) == 0 and number(13, 6) == 12
    assert number(19, 9) == 50
    assert [number(28 + index, 1) for index in range(7)] == [1, 1, 0, 0, 0, 0, 1]
    assert number(35, 16) == 0xffff
    assert [struct.unpack("<f", number(51 + 32 * index, 32).to_bytes(4, "little"))[0] for index in range(3)] == [0, 0, 0]
    assert number(147, 32) == 150
    assert len(out) // 2 == (147 + 32 + 7) // 8


def test_biped_update_matches_capture(harness):  # noqa: F811
    """A biped's update (version 3, full health and shield), as our capture
    of Custom Edition 1.0.10's server sent it for a client's own biped; the
    last byte's 3 unused bits are whatever the server's buffer held."""
    import subprocess
    out = subprocess.run([str(harness), "biped", "3"], capture_output=True, text=True, check=True).stdout.strip()
    captured = "088890310010f0ca"
    used = 61
    assert len(out) == len(captured)
    assert bits_of(out)[:used] == bits_of(captured)[:used]


def test_items_match_captures(harness):  # noqa: F811
    """Equipment lying in the world (the first message of our capture of
    Custom Edition 1.0.10's server after a join, 297 bits) and a dropped
    weapon's update (our capture of a plasma pistol let go: baseline 3, its
    place, no rounds, age 14), bit for bit; and a weapon swap's action
    result, field by field (its definition read from the retail binary)."""
    import subprocess
    lines = subprocess.run([str(harness), "items"], capture_output=True, text=True, check=True).stdout.split()
    assert bits_of(lines[0])[11:11 + 297] == bits_of(CAPTURED_WORLD)[11:11 + 297]
    captured_update = "1768883100d0273958c8742064b8ceb2d13700000080e3"
    assert len(lines[1]) == len(captured_update)
    assert bits_of(lines[1])[:180] == bits_of(captured_update)[:180]
    bits = bits_of(lines[2])
    number = lambda start, width: int(bits[start:start + width][::-1], 2)  # noqa: E731
    assert number(0, 11) == len(lines[2]) // 2
    assert number(11, 1) == 1 and number(12, 1) == 0 and number(13, 6) == 10
    assert number(19, 5) == 2 and number(24, 32) == 1 and number(56, 9) == 30
    assert number(65, 16) == 6 and number(81, 16) == 0xffff and number(97, 9) == 49
    assert len(lines[2]) // 2 == (106 + 7) // 8


# (our capture of Custom Edition 1.0.10's server after a join: its first
# vehicle's creation, the message's 301 bits)
CAPTURED_VEHICLE = (
    "10001110000001100100000000101011110001110100000001111111111111111000000000000001000000000000000000000"
    "00000000000000100000001001101100111010100111000100001000010110011001101000000101000011010111101011101"
    "100101011111111001111111111111111111000010100010100111000000000000000000000000000000000000000000011")


def test_vehicles_match_captures(harness):  # noqa: F811
    """A vehicle's creation and its first update (still), as our captures
    of Custom Edition 1.0.10's server sent them, bit for bit."""
    import subprocess
    lines = subprocess.run([str(harness), "vehicles"], capture_output=True, text=True, check=True).stdout.split()
    assert bits_of(lines[0])[11:11 + 301] == CAPTURED_VEHICLE
    captured_update = "20a8001010509004f43403eb25d01374e5878b4d001cba0a0db4a31a19a8ef9d"
    assert len(lines[1]) == len(captured_update)
    assert bits_of(lines[1])[:253] == bits_of(captured_update)[:253]
    # (a driver's vehicle, its own and another's: the fields their
    # definitions in the retail binary give; the vehicle's motion as the
    # update above, but moving)
    bits = bits_of(lines[2])
    number = lambda start, width: int(bits[start:start + width][::-1], 2)  # noqa: E731
    motion = bits_of(captured_update)[78:78 + 93]
    assert number(11, 1) == 1 and number(13, 6) == 36
    assert number(19, 5) == 9 and number(24, 6) == 33 and number(30, 9) == 2
    full = (1 << 31) - 1
    assert bits[39:39 + 93] == motion
    assert number(132, 1) == 0 and number(133, 31) == int((0.5 + 100) / 200 * full + 0.5)
    assert number(164, 31) == int(full / 2 + 0.5) and number(195, 31) == int(full / 2 + 0.5)
    assert number(226, 1) == 0 and number(289, 31) == int((-0.25 + 8) / 16 * full + 0.5)
    second = 320 + 80
    assert number(second, 1) == 1 and number(second + 2, 6) == 40
    assert number(second + 8, 5) == 3 and number(second + 13, 6) == 12 and number(second + 19, 3) == 5
    assert number(second + 22, 9) == 2 and bits[second + 31:second + 31 + 93] == motion


# (our capture of Custom Edition 1.0.10's server after a join: weapon
# creations, Slayer's scores (bits 4763 to 5795), a player added, the
# player's numbers in full (bits 5880 to 6182) and as a change)
CAPTURED_WORLD = (
    "11ebbb255831b7f0ff0f0000000000cc0140f3ff2cfedfffff0a5838fb0a81c0750c538a78ffdc774bb0627ee1ff1f00"
    "00000000980380e6ff59fcbfffff15681ef915b228ed18a614f1feb9ef9660c51cc3ff3f0000000000300700cdffb3f8"
    "7fffff2b70f50322a42b252a4c2de2fd73df2dc18a7986ff7f0000000000600e009aff67f1fffeff5760b3f84388443e"
    "54985ac4fbe7be5b8215730dffff0000000000c01c0034ffcfe2fffdffaf003b3688502979a830b588f7cf7db7042be6"
    "1bfeff010000000080390068fe9fc5fffbff5f01cf291021d1bc50616a11ef9ffb4e0936cc43fcff030000000000cd87"
    "e7c63fa2c300f8e402451fda42fc8fd8c2ecaf0340f7f514c49a8bf8ff070000000000ec160f387ca44e267e06058eae"
    "f184ea20378693a47d81eeeb2988351ff1ff0f0000000000d82d1e70f8489d4cfc0c0a9c26e109f5726d0caffbfd02dd"
    "d753106b4ee2ff1f0000000000b05b3ce0f0913a99f81914d033cd139a02db181ece0306baafa720d6bcc4ff3f000000"
    "000060b778c0e1237532f13328106f9127344fb3310c580f0c745f4f41acb989ff7f00000000002029eec2c8279888e2"
    "4950a0419b5268669653983dee16e8be9e8258f313ffff00000000004052dc85914f3011c593a000674ea5104c1aa7b0"
    "36f62dd07d3d05b1e628feff010000000080929dd3e61e8e475264410159a04a212e7d4e61c0485ca0fb7a0a62cd53fc"
    "ff030000000000253ba7cd3d1c8fa4c882028fae95429fb69cc29b1bb940f771124098bbf8ff07000000000030978f24"
    "8032faa044230506f6d784306bd385853ad77f0e02000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000e800000080630f00000000550100000000000000000000000000000000000000000000000000000000000000"
    "00000000c01500d0f0ffffff0f0000f823")


def test_scores_match_capture(harness):  # noqa: F811
    import subprocess
    out = subprocess.run([str(harness), "scores"], capture_output=True, text=True, check=True).stdout.strip()
    ours, captured = bits_of(out), bits_of(CAPTURED_WORLD)
    assert ours[11:11 + 1032] == captured[4763:5795]
    assert ours[11 + 1032:11 + 1032 + 302] == captured[5880:6182]


def test_response_matches_capture():
    # (a challenge and the answer Custom Edition 1.0.10's server gave to it)
    challenge = b".BUH5(RlWH%I[|W<+WS0KtIF]t%O7D{d"
    answer = b"xT*N>0DL?CIkGTev(%U>p17C!!&2f>2k"
    assert challenge_valid(challenge)
    assert matches(response(challenge), answer)


def test_join_is_refused_cleanly(harness):  # noqa: F811
    server = JoinServer(harness)
    client = Client(server.port)
    try:
        client.handshake()
        first = client.reliable()
        assert len(first) == 1 and first[0][-1] == 4
        challenge = first[0]
        assert challenge[0] == 1 and challenge[1:8].isalpha() and challenge[8] == 0
        assert challenge[18:-1] == b"xbox_bloodgulch\0"
        client.send_messages([join_request(), b"\x01" + b"xbox_bloodgulch".ljust(256, b"\0") + b"\x15"])
        refusal = client.reliable()
        assert refusal == [b"\x01\x00\x00\x00\x00\x06"]
        # (the refusal acknowledged: the server closes)
        client.send(b"\xfe\xfe\x64" + struct.pack(">H", client.in_sequence))
        assert client.receive() == b"\xfe\xfe\x68"
        assert server.wait_for_output("asks to join: \"Tester\"")
        # (the harness has no game: the join went to the game side, which
        # refused it)
        assert server.wait_for_output("harness: the join was refused (no game)")
    finally:
        server.stop()


def test_wrong_answer_closes(harness):  # noqa: F811
    server = JoinServer(harness)
    client = Client(server.port)
    try:
        mine = new_challenge(client.rng)
        client.send(b"\xfe\xfe\x01\x00\x00\x00\x00" + mine)
        reply = client.receive()
        assert reply[2] == 2
        client.send(b"\xfe\xfe\x03\x00\x01\x00\x01" + b"A" * 32 + (5).to_bytes(16, "big") + struct.pack("<I", 616999))
        assert client.receive() == b"\xfe\xfe\x68"
        # (the queries still answered)
        assert server.query(b"\\basic\\").startswith(b"\\hostname\\")
    finally:
        server.stop()


def test_off_by_default(harness):  # noqa: F811
    server = Server(harness)
    client = Client(server.port)
    client.sock.settimeout(0.5)
    try:
        client.send(b"\xfe\xfe\x01\x00\x00\x00\x00" + new_challenge(client.rng))
        with pytest.raises(socket.timeout):
            client.receive()
    finally:
        server.stop()


def _function(source, name):
    start = source.index("\n" + name + "(\n")
    return source[start:source.index("\n}\n", start) + 3]


def test_server_side_off_without_the_setting():
    """the game side (server/src/server_ce.c) does nothing unless ce_joins
    (and the GameSpy listing whose port takes the joins) is on: no events
    read, no player's input taken, no player found to kick, tell or time.
    No development switch is left"""
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    update = _function(source, "void server_ce_update")
    assert "ce.enabled = server_gamespy_enabled() && server_gamespy_joins();" in update
    assert update.index("if (!ce.enabled)\n\t{") < update.index("ce_transport_next_event(")
    for path in ("server/src/server_ce.c", "server/platform/gamespy_listing.c", "server/platform/ce_transport.c"):
        assert "HALO_DEDICATED_CE_ACCEPT" not in (ROOT / path).read_text(encoding="utf-8")
    assert "if (!ce.enabled ||" in _function(source, "boolean server_ce_local_action")
    assert "if (!ce.enabled)\n\t\treturn NULL;" in _function(source, "static struct ce_client *client_for_player")


def test_joins_held_to_host_limits():
    """a Custom Edition join is checked against the join lock, the bans and
    the limit on the machines of one address before it takes a seat"""
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    join = _function(source, "static boolean join")
    # (and none new while joins are locked: sv_lock keeps Custom Edition
    # players out as it does Chupathingy machines; a client back for the
    # next game was in already)
    assert join.index("CE_PHASE_RECONNECTING") < join.index("if (host_rules_joins_locked())")
    assert join.index("if (host_rules_joins_locked())") < join.index("network_game_client_add_named_player(")
    locked = join[join.index("if (host_rules_joins_locked())"):]
    assert locked.index("ce_transport_refuse(event->peer, CE_REJECT_FULL);") < locked.index("return TRUE;")
    assert join.index("network_distributed_banned(event->address, NULL)") < join.index("address_full(event->address)")
    assert join.index("address_full(event->address)") < join.index("network_game_client_add_named_player(")
    assert "network_distributed_address_full(address, others, count)" in \
        _function(source, "static boolean address_full")


# (our capture of Custom Edition 1.0.10's server ending a game for
# sv_map_next: the step after the scores came up, alone in its payload, and
# the step before them with game over, its data packet, after it)
CAPTURED_POSTGAME_3 = "07c81a00000080"
CAPTURED_POSTGAME_2_OVER = "3fc812000000c008100000000090d1b2"


def test_postgame_matches_capture(harness):  # noqa: F811
    import subprocess
    out = subprocess.run([str(harness), "postgame", "3"], capture_output=True, text=True, check=True).stdout.strip()
    used = 11 + 39
    assert bits_of(out)[:used] == bits_of(CAPTURED_POSTGAME_3)[:used]
    out = subprocess.run([str(harness), "postgame", "2", "over"], capture_output=True, text=True,
                         check=True).stdout.strip()
    used = 11 + 39 + 17 + 6 * 8
    assert bits_of(out)[11:used] == bits_of(CAPTURED_POSTGAME_2_OVER)[11:used]


def test_between_games_messages_match_capture():
    """the data packets of a game's end and the next game's start, as Custom
    Edition 1.0.10's server sent them in our capture: game over (25), the
    scores' keep-alive (13) and the reconnect (33: zero, then 1000 big-endian:
    the client closes its connection and connects again a second later)"""
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    header = (ROOT / "server/platform/ce_messages.h").read_text(encoding="utf-8")
    between = _function(source, "static boolean between_games")
    assert "game_over[] = { 1, 0, 0, 0, 0, CE_MESSAGE_GAME_OVER };" in between
    assert "keep_alive[] = { 1, 0, 0, CE_MESSAGE_POSTGAME_KEEP_ALIVE };" in between
    assert "reconnect[] = { 1, 0, 0, 0, 0, 0, 0, 3, 0xe8, CE_MESSAGE_RECONNECT };" in between
    for name, value in (("CE_MESSAGE_POSTGAME_KEEP_ALIVE", 13), ("CE_MESSAGE_GAME_OVER", 25),
                        ("CE_MESSAGE_RECONNECT", 33), ("CE_DELTA_POSTGAME_MODE", 22)):
        assert f"\t{name} = {value}," in header
    # (a reconnecting client's old connection closing keeps its seat; its new
    # connection is matched by address and name)
    update = _function(source, "void server_ce_update")
    assert "if (client && client->phase == CE_PHASE_RECONNECTING)\n\t\t\t\tclient->peer = NONE;" in update
    join = _function(source, "static boolean join")
    assert join.index("CE_PHASE_RECONNECTING") < join.index("network_distributed_banned(")


def test_only_custom_edition_maps():
    """Custom Edition players join and stay only while a Custom Edition map
    plays (the same .map file on both sides)"""
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    assert '!strcmp(at + 1, "ce")' in _function(source, "static boolean map_playable")
    assert "map_playable(status.map)" in _function(source, "static boolean join")
    assert "map_playable(status->map)" in _function(source, "static boolean between_games")


def test_welcome_line():
    """the default welcome line fits Custom Edition's chat"""
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    body = _function(source, "void server_ce_set_welcome")
    text = "".join(part for part in __import__("re").findall(r'"((?:[^"\\]|\\.)*)"', body.split("snprintf")[0]))
    assert text.startswith("Welcome to our Chupathingy Cross-Play Server.")
    assert len(text) <= 80 and text.isascii()


class JoinServer(Server):
    """the harness with Custom Edition's join wire on"""

    def __init__(self, harness):  # noqa: F811
        super().__init__(harness, extra=("joins",))

    def wait_for_output(self, text, seconds=3.0):
        import time
        end = time.time() + seconds
        while time.time() < end:
            if any(text in line for line in self.output):
                return True
            time.sleep(0.05)
        return False
