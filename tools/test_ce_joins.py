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
                        "zoom 65535")
    assert "ticks 3 flags 0" in lines[1] and "throttle 8" in lines[1]
    assert "ticks 2 flags 2" in lines[2] and "throttle 0" in lines[2]
    assert "ticks 4 flags 0" in lines[3]
    assert "yaw 0.1039" in lines[4] and "throttle 8" in lines[4]


def test_weapon_switch_from_capture(harness):  # noqa: F811
    """A 1.0.10 client's first update (no zoom, its grenade) and its switch
    to its second weapon (our capture, 2026-11-15) read as such."""
    import subprocess
    captured = ["0cb8290c90892c2b9ffeffb7", "06b809e29134"]
    lines = subprocess.run([str(harness), "updates", *captured], capture_output=True, text=True,
                           check=True).stdout.splitlines()
    assert "weapon 0 grenade 1 zoom 65535" in lines[0]
    assert "weapon 1 grenade 1 zoom 65535" in lines[1]


def test_weapons_follow_the_client():
    """A Custom Edition player's weapon, zoom and grenade choices reach the
    game, and a weapon put away is still its holder's (not dropped)"""
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    action = _function(source, "static void client_action")
    assert "server_ce_choices(client, player_index, &update, action);" in action
    choices = _function(source, "static void server_ce_choices")
    # each choice given once, as it changes (the game's own changes stand)
    assert "if (update->weapon != client->chosen_weapon)" in choices
    assert "if (update->zoom != client->chosen_zoom)" in choices
    # a new unit: the last unit's choices are not given again
    assert "client->chosen_unit != unit_index" in choices
    # a zoom level the weapon has, else none, given once the weapon it is
    # for is in hand (one asked during a switch waits for it)
    assert "client->pending_zoom < levels ? (short)client->pending_zoom : NONE" in choices
    assert "unit->unit.current_weapon_index == unit->unit.desired_weapon_index" in choices
    # the wire's grenade is the type itself (0 frag, 1 plasma: checked
    # against the client's HUD and a throw, 2026-11-14)
    assert "action->desired_grenade_index = (short)update->grenade;" in choices
    assert "grenade_counts[update->grenade] > 0" in choices
    # the biped messages carry the plasma count first (a 1.0.10 client's
    # HUD: frags only showed as plasmas before)
    messages = (ROOT / "server/platform/ce_messages.c").read_text(encoding="utf-8")
    assert messages.count("grenades[1], 3);\n\tput_bits(payload, (unsigned int)") == 2
    items = _function(source, "static void update_items")
    assert items.count("weapon_holder(") >= 3
    # (and held from then on, as the game holds a local player's)
    assert "client->weapon_choice = action->desired_weapon_index;" in action


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


def test_unanswered_challenges_hold_no_places(harness):  # noqa: F811
    """A connection's first packet is anyone's, from any address: challenges
    left unanswered, enough to take every place, keep no player out."""
    import time
    server = JoinServer(harness)
    squatters = []
    try:
        # (as fast as one address is answered, a few a second)
        for index in range(16):
            squatter = Client(server.port)
            squatter.send(b"\xfe\xfe\x01\x00\x00\x00\x00" + new_challenge(squatter.rng))
            assert squatter.receive()[2] == 2
            squatters.append(squatter)
            if index % 3 == 2:
                time.sleep(1.1)
        time.sleep(1.1)
        client = Client(server.port)
        client.handshake()
        assert client.reliable()[0][-1] == 4
    finally:
        for squatter in squatters:
            squatter.sock.close()
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
    # (the definition, not a prototype before it: a prototype's ";" comes
    # before any "{")
    while ";" in source[start:source.index("{", start)]:
        start = source.index("\n" + name + "(\n", start + 1)
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
    assert "if (!ce.enabled)\n\t\treturn;" in _function(source, "void server_ce_take_actions")
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
    assert join.index("if (host_rules_joins_locked())") < join.index("seat_client(client)")
    locked = join[join.index("if (host_rules_joins_locked())"):]
    assert locked.index("ce_transport_refuse(event->peer, CE_REJECT_FULL);") < locked.index("return TRUE;")
    assert join.index("network_distributed_banned(event->address, NULL)") < join.index("address_full(event->address)")
    assert join.index("address_full(event->address)") < join.index("seat_client(client)")
    assert "network_distributed_address_full(address, others, count)" in \
        _function(source, "static boolean address_full")
    # (16 players at most with Custom Edition players in: a CE client's game
    # holds 16; a server without them keeps its own limit)
    assert "game->player_count + coming >= CE_GAME_PLAYERS" in join
    manager = (ROOT / "source/networking/network_game_manager.c").read_text(encoding="utf-8")
    assert "if (game->player_count >= 16 && server_ce_players_present())" in manager


def test_lone_player_starts_a_team_game():
    """a Custom Edition player alone in an empty team game's lobby gets a
    game: its client gives up on a join in about 15 seconds, so the lobby
    doesn't wait for the other team while one is in (it did, and the first
    player into an empty Team Slayer or CTF server was turned away); without
    Custom Edition players the lobby waits for both teams, as the Xbox's
    does"""
    source = (ROOT / "source/networking/network_server_manager.c").read_text(encoding="utf-8")
    teams = _function(source, "boolean server_needs_more_teams")
    guard = teams.index("if (server_ce_players_present())\n\t\t\treturn FALSE;")
    assert teams.index("#ifdef HALO_SERVER") < guard < teams.index("universal_variant.teams")
    assert teams.count("#ifdef HALO_SERVER") == teams.count("#endif") == 1
    # (the countdown asks it)
    assert "boolean teams_full = !server_needs_more_teams(server);" in _function(source, "boolean server_ok_to_countdown")
    # (and the lobby's join is answered once that game plays)
    ce = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    assert "dedicated_server_start_soon();" in _function(ce, "static boolean join")


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
    # (a lobby of only Custom Edition players, seats of the server's own
    # machine, counts down to the next game without another machine)
    manager = (ROOT / "source/networking/network_server_manager.c").read_text(encoding="utf-8")
    countdown = _function(manager, "void network_game_server_update_countdown")
    assert "if (server_ce_players_present())\n\t\t\t\t\t\t\tother_players = TRUE;" in countdown


def test_only_custom_edition_maps():
    """Custom Edition players join and stay only while a Custom Edition map
    plays (the same .map file on both sides)"""
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    assert '!strcmp(at + 1, "ce")' in _function(source, "static boolean map_playable")
    assert "map_playable(status.map)" in _function(source, "static boolean join")
    assert "map_playable(status->map)" in _function(source, "static boolean between_games")


def test_team_slayer():
    """Team Slayer reaches a Custom Edition client: the game's own variant
    over Custom Edition's Slayer, each player's team in the roster (sent
    again as players come, go or change teams), and a second client told
    its own machine number, so it finds its player in that roster"""
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    variant = _function(source, "static void settings_from_variant")
    assert "settings->universal[0] = universal->teams ? 1 : 0;" in variant
    assert "settings->universal[15] = (unsigned int)universal->score_to_win;" in variant
    assert "!engine_told(variant->game_engine_index)" in variant
    roster = _function(source, "static void game_settings_for")
    assert "team = network_player_team(player);" in roster
    assert "settings->random_seed = ce.random_seed;" in roster
    assert "send_roster(client);" in _function(source, "static void update_client")
    assert "client->announced_team[absolute] != player->team_index" in source
    transport = (ROOT / "server/platform/ce_transport.c").read_text(encoding="utf-8")
    assert "out[13] = (unsigned char)machine;" in transport
    assert "ce_message_new_client_challenge(challenge_text, peer_number(peer)," in transport
    messages = (ROOT / "server/platform/ce_messages.c").read_text(encoding="utf-8")
    assert "put_bits(payload, (unsigned int)team, 32);" in messages


def test_ctf_matches_capture(harness):  # noqa: F811
    """Capture the Flag's messages as our capture of Custom Edition's own
    server has them (research capture real-ce-ctf-grab-drop): the grab (the
    team's sound, the flag's HUD note, the pickup), a carrier's drop, a HUD
    message, and a new game's state (no swap timer, no scores, no team)"""
    import subprocess

    def run(step):
        return subprocess.run([str(harness), "ctf", step], capture_output=True, text=True, check=True).stdout.strip()

    # (the grab's 210 bits; the last byte's unused bits are whatever the
    # server's buffer held)
    assert bits_of(run("grab"))[:210] == bits_of("1b285b000000c8b0365042070000480901000000020e00feff0128")[:210]
    assert run("drop") == "0568a32120"
    assert run("message") == "0b080b1e00000078690000"
    state = bits_of(run("state"))
    # (a delta, all fields, type 17; then 104 zero bits)
    assert state[11:19] == "10100010" and set(state[19:19 + 104]) == {"0"}


def test_ctf_reaches_custom_edition():
    """Capture the Flag is shown as it is: the game type and its own settings,
    the flags' teams, its state as it changes, the HUD's messages and the
    game's sounds, and a carrier's drop"""
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    assert "engine == game_engine_slayer || engine == game_engine_ctf" in _function(source, "static boolean engine_told")
    assert "csmemcpy(settings->engine_variant, &variant->game_engine_variant," in _function(
        source, "static void settings_from_variant")
    assert "object->object.owner_team_index == NONE ? 0xffff" in _function(source, "static void put_object_start")
    assert "ce_payload_put_ctf_update(&payload" in _function(source, "static void send_scores")
    assert "ce_payload_put_unit_drop_weapon(reliable" in _function(source, "static void update_items")
    assert "ce_payload_put_hud_add_item(reliable" in _function(source, "static void update_items")
    assert "ce.showing_message" in _function(source, "void server_ce_multiplayer_sound")
    engine = (ROOT / "source/game/game_engine.c").read_text(encoding="utf-8")
    assert "server_ce_multiplayer_message(player_index, message, message_data);" in engine
    sounds = (ROOT / "source/game/game_engine_multiplayer_sounds.c").read_text(encoding="utf-8")
    assert "server_ce_multiplayer_sound(sound_index);" in sounds
    # (a capture's message shows the scores its client has: they go first)
    message = _function(source, "void server_ce_multiplayer_message")
    assert message.index("send_scores(client);") < message.index("ce_payload_put_hud_message(")
    # (a game type's messages are two further on in Custom Edition's list:
    # Capture the Flag's capture, 31 here, is its 33, "You scored 1 to 0")
    assert "ce_message_number(message)" in message
    numbers = _function(source, "static unsigned int ce_message_number")
    assert "FIRST_GAME_TYPE_MESSAGE = 30," in source and "CE_FIRST_GAME_TYPE_MESSAGE = 32," in source
    # (not a game type shown as Slayer (Bomb): its messages would read as
    # Slayer's; a named player outside the client's roster, none)
    assert "message >= FIRST_GAME_TYPE_MESSAGE &&" in message and "engine_told(" in message
    assert "!network_player_number(client, data)" in message
    assert "!network_player_number(client, data))\n\t\t{\n\t\t\tcontinue;" in message
    # (one flag: the flag's team in its state, and the flag made again as
    # its sides swap, as Custom Edition's own server does (research capture
    # rs1, Assault): a client takes an object's team only at its creation)
    scores = _function(source, "static void send_scores")
    assert "team_with_flag = flag->object.owner_team_index;" in scores
    items = _function(source, "static void update_items")
    assert "TEST_FLAG(_object_mask_weapon, item->object.type) && weapon_is_flag(client->item[number])" in items


def test_roster_players_get_their_numbers():
    """a roster takes the first 15 other players in the game's order,
    whatever their index: each one's numbers reach its client, and a player
    the client's updates can't carry is not listed"""
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    assert "struct ce_player_statistics statistics_sent[TRACKED_PLAYERS];" in source
    scores = _function(source, "static void send_scores")
    assert "absolute >= TRACKED_PLAYERS || client->announced[absolute] != iterator.datum_index" in scores
    roster = _function(source, "static void game_settings_for")
    assert "DATUM_INDEX_TO_ABSOLUTE_INDEX(datum) >= TRACKED_PLAYERS" in roster
    told = _function(source, "static void roster_told")
    assert "csmemset(client->statistics_known, 0, sizeof(client->statistics_known));" in told


def test_game_types_match_captures(harness):  # noqa: F811
    """Oddball's, King's and Race's state as our captures of Custom Edition's
    server have it at a join (research captures odd1, king1, race1: a new
    game's, all zero): the message and its length, the next message's start
    found right after it"""
    import subprocess

    for kind, count, length in ((18, 48, 1536), (19, 17, 544), (20, 50, 1600)):
        out = subprocess.run([str(harness), "gametype", str(kind), str(count)], capture_output=True, text=True,
                             check=True).stdout.strip()
        bits = bits_of(out)
        assert bits[11:19] == "10" + "".join(str((kind >> i) & 1) for i in range(6))
        assert int(bits[:11][::-1], 2) == (11 + 8 + length + 7) // 8
        assert set(bits[19:19 + length]) == {"0"}
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    told = _function(source, "static boolean engine_told")
    for engine in ("game_engine_oddball", "game_engine_king", "game_engine_race"):
        assert engine in told
    assert "ce_payload_put_game_type_update(&payload, type, values, count);" in _function(source, "static void send_scores")
    # (Oddball's and King's times go in seconds, as a Custom Edition client
    # counts them; the ball's five-second beep is its carrier's client's)
    state = _function(source, "static int game_type_state")
    assert state.count("/= TICKS_PER_SECOND;") == 2
    # (and told as a client joins: its client counts them itself)
    scores = _function(source, "static void send_scores")
    assert "changed = values[16] != client->game_type_sent[16];" in scores
    # (a free for all's teams are roster slots, one per player on a client,
    # in the roster, the spawns, the bipeds and the game type's numbers)
    # (kept by each player while it stays in the roster, so a biped's team
    # never goes stale as players before it leave; one test of team or free
    # for all for the roster and the players' and bipeds' teams: Bomb, shown
    # as Slayer, is a free for all everywhere)
    assert "client->ffa_player[absolute] == player_index" in _function(source, "static int client_team")
    assert "if (!ce_team_game() && player_index != NONE)" in _function(source, "static int client_team")
    ffa = _function(source, "static void ffa_teams")
    assert "team = client->ffa_player[absolute] == player_index ? client->ffa_team[absolute] : -1;" in ffa
    assert "engine_told(variant->game_engine_index) && variant->universal_variant.teams" in \
        _function(source, "static boolean ce_team_game")
    roster = _function(source, "static void game_settings_for")
    assert "settings->universal[0] ?" not in roster and "game_engine_has_teams()" not in source
    assert "ffa_teams_told(client, &settings, slot_players);" in _function(source, "static void send_roster")
    assert "(out)[client_team(client, player_index)] = (unsigned int)(array)[own_team];" in state
    assert source.count("client_team(client, player_index)") == 4
    statistics = _function(source, "static void player_statistics")
    assert "statistics->game_specific[0] = (specific[0] / TICKS_PER_SECOND) & 0xffff;" in statistics
    sound = _function(source, "void server_ce_multiplayer_sound")
    assert "sound_index == SOUND_COUNTDOWN_TIMER_END && game_engine &&" in sound


def test_custom_edition_clients_are_machines():
    """each Custom Edition client is a machine of the game of its own (a
    virtual machine: no connection, one player at controller 0), not a
    split-screen seat of the server's machine (4 at most, the controller
    index every client checks): 16 of them fit a game. Nothing new on the
    wire: the other machines see one more machine and player"""
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    assert "CE_CLIENTS = CE_MAXIMUM_PEERS," in source
    assert "local_player_get_player_index" not in source
    assert "network_game_client_add_named_player" not in source
    seat = _function(source, "static boolean seat_client")
    assert "network_game_server_port_add_virtual_machine(wide_name, client->address)" in seat
    assert "network_game_server_port_add_virtual_player(client->machine_index, wide_name, 0)" in seat
    assert "network_game_server_port_remove_virtual_machine(client->machine_index);" in \
        _function(source, "static void leave_client")
    # (its player's input taken each update the host builds, as a client
    # machine's player's is)
    take = _function(source, "void server_ce_take_actions")
    assert "update_server_handle_distributed_input(player_index, update_number, &action, &control_flags, 1);" in take
    queues = (ROOT / "source/game/player_queues_new.c").read_text(encoding="utf-8")
    next_update = _function(queues, "void update_server_next_update")
    assert next_update.index("update_server_take_local_actions();") < next_update.index("server_ce_take_actions(update_number);")
    control = (ROOT / "source/game/player_control.c").read_text(encoding="utf-8")
    assert "server_ce_local_action" not in control
    assert "server_ce_set_desired_weapon(unit->unit.player_index, desired_weapon_index);" in control
    assert "server_ce_unzoom(player_index);" in control
    # (the host's machine loops: a virtual machine is never read, timed out
    # or reset to loading; a kick of one lets its client go)
    manager = (ROOT / "source/networking/network_server_manager.c").read_text(encoding="utf-8")
    loop = _function(manager, "static boolean network_game_server_handle_client_machines")
    assert loop.index("network_game_server_client_machine_is_virtual(client_machine)") < \
        loop.index("network_connection_active(client_machine->connection)")
    assert "server_ce_machine_dropped(machine_index);" in loop
    assert "network_game_server_client_machine_is_virtual(machine)" in \
        _function(manager, "static boolean network_game_server_client_machine_timed_out")
    assert "network_game_server_client_machine_is_virtual(&server->client_machines[i])" in \
        _function(manager, "boolean network_game_server_reset_to_pregame")
    assert "!network_game_server_client_machine_is_virtual(client_machine)" in \
        _function(manager, "boolean network_game_server_change_map_name")
    virtual = _function(manager, "long network_game_server_port_add_virtual_machine")
    assert "FLAG(_network_client_machine_level_loaded_bit) | FLAG(_network_client_machine_precached_bit)" in virtual
    assert "server->client_machines[index].connection = NULL;" in virtual
    # (its players' hits are the host's to deal: no report comes from it)
    damage = (ROOT / "port/linux/game/network_damage.c").read_text(encoding="utf-8")
    assert "!network_game_server_port_machine_is_virtual(owner->network_player_data.machine_index)" in damage
    # (and Delta's view of the game leaves it out: no address, not the
    # host's, so the capabilities every machine shares are not cut by it)
    delta = _function(manager, "static void network_game_server_delta_frame")
    assert "if (local || machines[count].ipv4)" in delta
    # (nor judged by Delta's rules for a game: it never says HELLO, and its
    # slot's record may be a machine's before it)
    refusals = _function(manager, "static void network_game_server_delta_refusals")
    assert "network_game_server_client_machine_is_virtual(machine) ||" in refusals
    # (a client's player and its lookups by machine hold only while that
    # machine is still its virtual one)
    assert "!network_game_server_port_machine_is_virtual(client->machine_index)" in \
        _function(source, "static long client_player")
    assert "controller_index == 0 && network_game_server_port_machine_is_virtual(machine_index)" in \
        _function(source, "static struct ce_client *client_for_player")


def test_welcome_line():
    """the default welcome line fits Custom Edition's chat, and comes once,
    a minute into a player's first game"""
    source = (ROOT / "server/src/server_ce.c").read_text(encoding="utf-8")
    body = _function(source, "void server_ce_set_welcome")
    text = "".join(part for part in __import__("re").findall(r'"((?:[^"\\]|\\.)*)"', body.split("snprintf")[0]))
    assert text == "Playing on Chupathingy: free, runs Halo PC and Xbox maps. chupathingy.org"
    assert len(text) <= 80 and text.isascii()
    assert "WELCOME_DELAY_MILLISECONDS = 60000" in source
    assert "system_milliseconds() - client->joined_time >= WELCOME_DELAY_MILLISECONDS" in source
    assert "client->welcomed = kept.welcomed;" in source


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
